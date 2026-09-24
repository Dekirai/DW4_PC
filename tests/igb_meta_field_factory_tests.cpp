#include "dw4/igb_meta_field_factory.hpp"
#include "dw4/igb_field_operations.hpp"

#include <iostream>

namespace {
using namespace dw4;using K=IgbMetaFieldServiceKind;using B=IgbBootstrapServiceKind;using S=IgbDependenciesServiceKind;
unsigned checks{};
void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
template<class F>void rejects(F&& f,const char* why){++checks;try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t size=0x34){auto storage=std::make_shared<IgbObjectStorage>();
    storage->original_address=address;storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(size,0xcc);storage->bytes->initialized.assign(size,1);return {storage,0};}
struct Fixture {
    std::shared_ptr<IgbMetaFieldFactoryServices> services=std::make_shared<IgbMetaFieldFactoryServices>();
    std::shared_ptr<IgbDecodeRuntimeField> field=std::make_shared<IgbDecodeRuntimeField>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    IgbBootstrapPointer object=allocation(0x100000);
    std::vector<IgbBootstrapCall> raw;std::vector<IgbMetaFieldCall> calls;std::vector<IgbDependenciesCall> strings;
    IgbBootstrapFactory raw_factory;IgbMetaFieldFactory factory;IgbDependenciesFactory string_factory;
    Fixture(){
        object.storage->bindings.push_back({0,{},{},{},{},field});
        services->bootstrap=std::make_shared<IgbBootstrapServices>();services->bootstrap->runtime=std::make_shared<IgbRuntimeTypesServices>();
        services->bootstrap->runtime->singleton=core;services->bootstrap->default_pool_handle=7;
        auto metadata=std::make_shared<IgbRuntimeMetaType>();metadata->allocation_prefix=0;
        services->bootstrap->object_metadata=std::make_shared<IgbRegistrationMetaSlot>(metadata);
        services->metadata=std::make_shared<IgbRegistrationMetaSlot>();services->dependencies=std::make_shared<IgbDependenciesServices>();
        services->dependencies->globals=std::make_shared<SystemStageServices>();services->dependencies->globals->string_pool=pool;
        services->bootstrap->factory=[this](const IgbBootstrapCall& c){raw.push_back(c);if(raw_factory)return raw_factory(c);
            auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[this,c,out=op.get()]{
                if(c.kind==B::pool_for_handle || c.kind==B::allocation_pool)out->pool_result=pool;
                else if(c.kind==B::pool_allocate || c.kind==B::heap_allocate)out->allocation_result=object;
                return MessengerCallResult{true,c.kind==B::pool_index?0xabcdef37u:0u,{}};};return op;};
        services->factory=[this](const IgbMetaFieldCall& c){calls.push_back(c);return factory?factory(c):std::shared_ptr<IgbMetaFieldOperation>{};};
        services->dependencies->factory=[this](const IgbDependenciesCall& c){strings.push_back(c);return string_factory?string_factory(c):std::shared_ptr<IgbDependenciesOperation>{};};
    }
};
void exact_constructed_bytes(){
    for(const auto entry:{IgbMetaFieldConstructor::specialized,IgbMetaFieldConstructor::base,IgbMetaFieldConstructor::reset,IgbMetaFieldConstructor::specialized_reset}){
        Fixture f;IgbMetaFieldConstructorInvocation call(f.object,entry);check(call.resume(f.services).completed && call.result()==f.field,"Concrete descriptor constructor failed.");
        const auto vt=entry==IgbMetaFieldConstructor::specialized || entry==IgbMetaFieldConstructor::specialized_reset?0x80476e6cu:0x80471914u;
        auto expected=std::vector<std::uint8_t>(0x34,0xcc);
        const auto word=[&](unsigned at,std::uint32_t value){for(unsigned j=0;j<4;++j)expected[at+j]=static_cast<std::uint8_t>(value>>((3-j)*8));};
        word(0,vt);word(4,1);for(const auto at:{8u,12u,0x18u,0x1cu,0x20u,0x30u})word(at,0);
        expected[0x10]=expected[0x11]=0xff;for(const auto at:{0x12u,0x13u,0x14u,0x15u,0x24u,0x25u,0x27u,0x28u,0x29u,0x2au})expected[at]=0;
        expected[0x26]=expected[0x2b]=1;expected[0x2c]=2;
        check(f.object.storage->bytes->bytes==expected,"Descriptor stores or untouched padding differ from the original constructor.");
        check(f.field->vtable_word==vt && f.field->reference_word==1 && f.field->offset==0 && f.field->type_ordinal==0xffff && f.field->ordinal==0 && f.field->size==0,"Descriptor canonical header/layout differs from physical stores.");
        check(f.field->name_string && !*f.field->name_string && f.field->name_string_address==0 && f.field->default_bytes && !f.field->default_bytes->buffer && f.field->default_bytes_address==0,"Descriptor reset invented strings/default allocation.");
        check(f.field->declaring_type && igb_field_declaring_type_is_null(*f.field->declaring_type) && f.field->global_slot && igb_field_global_slot_is_null(*f.field->global_slot) && f.field->global_slot_address==0,"Descriptor reset failed to establish known null pointer slots.");
        check(f.field->flag_26==1 && f.field->flag_25==0 && f.field->flag_2b==1 && f.field->flag_2c==2 && f.field->default_mode==0 && f.field->word_30==0,"Descriptor flag defaults differ.");
        check(f.strings.empty() && f.calls.empty(),"Original null-name leaf was delegated to a fake interner or descriptor virtual.");
    }
    Fixture f;IgbMetaFieldFactoryInvocation factory(f.pool);check(factory.resume(f.services).completed && factory.result()==f.field,"Disabled22A90 failed native construction.");
    check(f.raw.size()==1 && f.raw[0].kind==B::pool_allocate && f.raw[0].pool==f.pool && f.raw[0].bytes==0x34,"Specialized factory changed original allocation size/pool.");
    check(f.services->completed_allocations.size()==1 && factory.allocation().storage==f.object.storage,"Completed factory lost the physical descriptor owner.");
    check(factory.resume().completed && f.raw.size()==1,"Completed descriptor factory replayed allocation.");
}
void double_reset_and_pending(){
    Fixture f;f.services->dependencies->globals->string_pool.reset();unsigned selections{},polls{};
    f.string_factory=[&](const IgbDependenciesCall& c){check(c.kind==S::create_string_pool && c.bytes==16,"Reset skipped the exact string-pool allocation caller.");++selections;
        auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[&]{++polls;return MessengerCallResult{polls!=1,0,{}};};return op;};
    IgbMetaFieldFactoryInvocation call(f.pool);check(!call.resume(f.services).completed && selections==1,"Selected string allocator did not suspend.");
    check(f.field->vtable_word==0x80471914 && igb_bootstrap_read_byte(f.object,0x26)==0xcc && f.field->type_ordinal==0xffff,"Pending reset lost its actual partial-store prefix.");
    check(call.resume(f.services).completed && selections==2 && polls==3,"Specialized constructor omitted its second actual reset or replaced the pending callback.");
    check(f.field->vtable_word==0x80476e6c && !f.services->dependencies->globals->string_pool,"Null string-pool results were replaced with a fabricated initialized pool.");

    Fixture changed;changed.raw_factory=[&](const IgbBootstrapCall& c){auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[&,c,out=op.get()]{
        if(c.kind==B::pool_allocate){changed.core->enabled=1;out->allocation_result=changed.object;}
        if(c.kind==B::allocation_pool)out->pool_result=changed.pool;
        return MessengerCallResult{true,0x12345678,{}};};return op;};
    IgbMetaFieldFactoryInvocation live(changed.pool);check(live.resume(changed.services).completed && changed.raw.size()==3 && changed.raw[1].kind==B::allocation_pool && changed.raw[2].kind==B::pool_index,"Constructor reused the factory's old enabled byte.");
    check(changed.field->reference_word==1,"Second base reset failed to clear the newly assigned pool tag.");
}
void injected_name_release_and_owners(){
    Fixture f;f.services->dependencies->globals->string_pool.reset();auto old=std::make_shared<IgbInternedString>();
    old->value=std::make_shared<const std::string>("old");old->container=f.pool;
    auto replacement=std::make_shared<IgbInternedString>();replacement->value=std::make_shared<const std::string>("replacement");replacement->references=17;
    bool released{};unsigned releases{},polls{};
    f.string_factory=[&](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[&,c,out=op.get()]{
        if(c.kind==S::create_string_pool){out->pool_result=f.pool;igb_meta_field_set_name(f.object,old,0x200000);}
        else if(c.kind==S::initialize_string_pool){check(c.pool==f.pool,"String initializer lost its captured pool.");out->pool_result=f.pool;}
        else if(c.kind==S::release_string){++polls;check(c.string==old && c.pool==f.pool && old->references==0,"Reset did not decrement the live injected full string reference word before release.");
            if(!released)return MessengerCallResult{false,0,"prepared release"};++releases;igb_meta_field_set_name(f.object,replacement,0x210000);}
        else throw std::runtime_error("Unexpected string operation");return MessengerCallResult{true,0,{}};};return op;};
    IgbMetaFieldConstructorInvocation call(f.object);
    check(!call.resume(f.services).completed && polls==1 && old->references==0,"Injected live name did not suspend at real release boundary.");
    released=true;check(call.resume(f.services).completed && polls==2 && releases==1 && replacement->references==17,"Completed release was replayed or raw replacement got a guest decrement.");
    check(igb_bootstrap_read_word(f.object,12)==0 && !f.field->name && !*f.field->name_string,"Original raw name publication did not overwrite callback replacement.");
    std::weak_ptr<IgbInternedString> weak=replacement;replacement.reset();check(!weak.expired(),"Raw overwritten callback name was implicitly freed.");

    Fixture raw;auto original=std::make_shared<IgbInternedString>();original->references=29;igb_meta_field_set_name(raw.object,original,0x220000);
    IgbMetaFieldConstructorInvocation reset(raw.object,IgbMetaFieldConstructor::reset);check(reset.resume(raw.services).completed && original->references==29,"Constructor's initial raw name clear was converted to a reference assignment.");
    Fixture nonowning;auto global=std::make_shared<IgbRuntimeFieldSlot>(nonowning.field);std::weak_ptr<IgbRuntimeFieldSlot> weak_global=global;
    nonowning.field->global_slot=global;nonowning.field->global_slot_address=0x805622a8;global.reset();
    check(weak_global.expired() && !igb_field_global_slot_is_null(*nonowning.field->global_slot),"Descriptor global binding formed an owning cycle or lost expired pointer identity.");
    auto declaring=std::make_shared<IgbRuntimeMetaType>();nonowning.field->declaring_type=declaring;
    declaring.reset();check(!igb_field_declaring_type_is_null(*nonowning.field->declaring_type) && nonowning.field->declaring_type->expired(),"Declaring type binding retained its owner or confused expired and null pointers.");
    IgbMetaFieldConstructorInvocation overwrite(nonowning.object,IgbMetaFieldConstructor::reset);
    check(overwrite.resume(nonowning.services).completed && igb_field_global_slot_is_null(*nonowning.field->global_slot),"Raw constructor reset dereferenced or retained a replaced nonowning global cell.");
    check(igb_field_declaring_type_is_null(*nonowning.field->declaring_type),"Raw constructor reset retained or dereferenced a replaced declaring type.");
}
void enabled_branch_and_faults(){
    Fixture enabled;enabled.core->enabled=1;auto first=std::make_shared<IgbRuntimeMetaType>(),fresh=std::make_shared<IgbRuntimeMetaType>();*enabled.services->metadata=first;
    enabled.services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();unsigned bodies{};
    enabled.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){check(c.function==0x80022cd8,"Factory selected a different registration body.");
        auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++bodies;*enabled.services->metadata=fresh;return MessengerCallResult{true,0,{}};};return op;};
    enabled.factory=[&](const IgbMetaFieldCall& c){check(c.kind==K::reflected_factory && c.pool==enabled.pool && c.metadata==fresh,"Enabled factory failed to reload its canonical metadata cell.");
        auto op=std::make_shared<IgbMetaFieldOperation>();op->resume=[&,out=op.get()]{out->allocation_result=enabled.object;return MessengerCallResult{true,0,{}};};return op;};
    IgbMetaFieldFactoryInvocation reflected(enabled.pool);check(reflected.resume(enabled.services).completed && reflected.result()==enabled.field && bodies==1 && enabled.raw.empty(),"Enabled factory replaced explicit reflection with direct construction.");
    check(igb_bootstrap_read_word(enabled.object,0)==0xcccccccc,"Enabled factory silently initialized the prepared reflected result.");
    Fixture pending;pending.core->enabled=1;pending.services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();IgbMetaFieldFactoryInvocation unbound;
    check(!unbound.resume(pending.services).completed && pending.raw.empty(),"Unknown registration body became successful construction.");
    Fixture null;null.raw_factory=[](const IgbBootstrapCall&){auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[]{return MessengerCallResult{true,0,{}};};return op;};
    IgbMetaFieldFactoryInvocation missing(null.pool);rejects([&]{missing.resume(null.services);},"Null object allocation bypassed original tail dereference.");
    Fixture bytes;bytes.object.storage->bytes->initialized[4]=0;IgbMetaFieldConstructorInvocation unreadable(bytes.object);
    rejects([&]{unreadable.resume(bytes.services);},"Base constructor omitted the old reference-word read.");check(igb_bootstrap_read_word(bytes.object,0)==0x804717f0,"Failed base read lost original vtable store prefix.");
    Fixture conflict;conflict.object.storage->bindings.push_back({0,{},{},{},std::make_shared<IgbRuntimeTypeList>()});
    IgbMetaFieldFactoryInvocation crossed(conflict.pool);rejects([&]{crossed.resume(conflict.services);},"Descriptor reused a conflicting metadata-list allocation identity.");
    Fixture held;held.services->dependencies->globals.reset();{IgbMetaFieldConstructorInvocation partial(held.object);check(!partial.resume(held.services).completed,"Missing canonical string global completed reset.");}
    IgbMetaFieldConstructorInvocation restart(held.object);rejects([&]{restart.resume(held.services);},"Abandoned descriptor reset restarted from an invented clean state.");
    std::weak_ptr<IgbDecodeRuntimeField> weakfield;std::weak_ptr<IgbObjectStorage> weakstorage;
    {Fixture no_owner;weakfield=no_owner.field;weakstorage=no_owner.object.storage;IgbMetaFieldConstructorInvocation pending_ctor(no_owner.object);check(!pending_ctor.resume().completed,"Constructor without services completed.");}
    check(weakfield.expired() && weakstorage.expired(),"Abandoned descriptor created a field/allocation ownership cycle.");
}
struct ProfileEvidence {
    IgbMetaFieldKind kind;std::uint32_t factory,constructor,reset,vtable,bytes,registration,callback,global;
};
constexpr ProfileEvidence evidence[]={
    {IgbMetaFieldKind::unsigned_int,0x80022a90,0x800759e0,0x80075a2c,0x80476e6c,0x34,0x80022cb0,0x80022cd8,0x805614bc},
    {IgbMetaFieldKind::signed_int,0x8002e1ac,0x8005356c,0x800535b8,0x80475b60,0x34,0x8002e390,0x8002e3b8,0x805619e0},
    {IgbMetaFieldKind::boolean,0x800385e0,0x8003eb7c,0x8003ebc8,0x80473e30,0x34,0x80038800,0x80038828,0x80561e48},
    {IgbMetaFieldKind::string,0x800251ec,0x80071df8,0x80071e44,0x8047693c,0x3c,0x800253dc,0x80025404,0x805615b4},
    {IgbMetaFieldKind::object,0x8002879c,0x8006ac50,0x8006aca4,0x804762fc,0x44,0x800289f8,0x80028a20,0x805616e8},
    {IgbMetaFieldKind::pointer,0x800274ec,0x8006c46c,0x8006c4b8,0x80476630,0x38,0x800276dc,0x80027704,0x80561694},
    {IgbMetaFieldKind::unsigned_short,0x80021e10,0x80075ec0,0x80075f0c,0x80477054,0x34,0x80021ff4,0x8002201c,0x80561494},
    {IgbMetaFieldKind::memory_reference,0x8002ac34,0x8006324c,0x800632a4,0x80476088,0x50,0x8002ae84,0x8002aeac,0x80561818},
    {IgbMetaFieldKind::signed_short,0x800259a8,0x800710bc,0x80071108,0x80476848,0x34,0x80025b8c,0x80025bb4,0x805615d8},
    {IgbMetaFieldKind::signed_byte,0x80037b1c,0x8003ff44,0x8003ff90,0x80473f24,0x34,0x80037d00,0x80037d28,0x80561e1c},
    {IgbMetaFieldKind::enumeration,0x80031378,0x80046f8c,0x80046fd8,0x804758e4,0x38,0x8003155c,0x80031584,0x80561c30}
};
bool ref_kind(IgbMetaFieldKind kind){return kind==IgbMetaFieldKind::string || kind==IgbMetaFieldKind::object || kind==IgbMetaFieldKind::pointer || kind==IgbMetaFieldKind::memory_reference;}
void resize(Fixture& f,std::size_t size){f.object.storage->bytes->bytes.assign(size,0xcc);f.object.storage->bytes->initialized.assign(size,1);}
void family_constructor_bytes(){
    for(const auto& p:evidence){
        const auto& profile=igb_meta_field_profile(p.kind);
        check(profile.factory==p.factory && profile.constructor==p.constructor && profile.reset==p.reset && profile.vtable==p.vtable && profile.bytes==p.bytes &&
            profile.registration==p.registration && profile.callback==p.callback && profile.metadata_global==p.global,"Recovered class profile differs from original entry identities.");
        check(igb_meta_field_kind_for_factory(p.factory)==p.kind,"Original factory identity lost its class profile.");
        for(const auto entry:{IgbMetaFieldConstructor::specialized,IgbMetaFieldConstructor::specialized_reset}){
            Fixture f;resize(f,p.bytes);IgbMetaFieldConstructorInvocation call(f.object,entry,p.kind);
            check(call.resume(f.services).completed && call.result()==f.field,"Specialized constructor family did not complete.");
            auto expected=std::vector<std::uint8_t>(p.bytes,0xcc);
            const auto word=[&](unsigned at,std::uint32_t value){for(unsigned j=0;j<4;++j)expected[at+j]=static_cast<std::uint8_t>(value>>((3-j)*8));};
            word(0,p.vtable);word(4,1);for(const auto at:{8u,12u,0x18u,0x1cu,0x20u,0x30u})word(at,0);
            expected[0x10]=expected[0x11]=0xff;
            for(const auto at:{0x12u,0x13u,0x14u,0x15u,0x24u,0x25u,0x27u,0x28u,0x29u,0x2au})expected[at]=0;
            expected[0x26]=expected[0x2b]=1;expected[0x2c]=2;
            if(ref_kind(p.kind)){expected[0x34]=expected[0x35]=0;check(f.field->flag_34==0,"Reference base omitted flag34 reset.");}
            if(p.kind==IgbMetaFieldKind::enumeration)check(!f.field->method_function && !f.field->flag_34 &&
                igb_bootstrap_read_word(f.object,0x34)==0xcccccccc,
                "Enum reset initialized its untouched function word or performed reference-base stores.");
            if(p.kind==IgbMetaFieldKind::string){expected[0x35]=expected[0x38]=1;expected[0x27]=2;
                check(f.field->flag_38==1 && f.field->flag_35==1 && f.field->flag_27==2,"String descriptor flags differ.");}
            else if(p.kind==IgbMetaFieldKind::object){word(0x38,0);word(0x40,0);expected[0x3c]=1;
                check(f.field->object_type && igb_field_declaring_type_is_null(*f.field->object_type) && f.field->word_40==0 && f.field->flag_3c==1,"Object descriptor type/cache/flag defaults differ.");}
            else if(p.kind==IgbMetaFieldKind::memory_reference){
                word(0x38,0);word(0x3c,0);word(0x44,0xffffffffu);word(0x48,0);word(0x4c,0);
                expected[0x40]=expected[0x41]=expected[0x42]=1;expected[0x24]=3;expected[0x27]=2;
                check(f.field->element_field && f.field->element_field->expired() && f.field->word_38==0 && f.field->word_44==0xffffffffu &&
                    f.field->word_48==0 && f.field->word_4c==0 && f.field->flag_40==1 && f.field->flag_41==1 && f.field->flag_42==1 &&
                    f.field->default_mode==3 && f.field->flag_27==2,"Memory-reference descriptor defaults differ.");
            }
            check(f.object.storage->bytes->bytes==expected,"Specialized descriptor physical stores or untouched padding differ.");
            check(f.field->physical_storage && f.field->physical_storage->storage.lock()==f.object.storage && f.field->physical_storage->offset==0,"Constructor lost canonical weak physical binding.");
            check(f.field->vtable_word==p.vtable && f.field->reference_word==1 && f.field->size==0 && f.calls.empty(),"Constructor delegated known original work or changed its canonical result.");
        }
    }
    for(const auto entry:{IgbMetaFieldConstructor::reference_base,IgbMetaFieldConstructor::reference_reset}){
        Fixture f;resize(f,0x38);IgbMetaFieldConstructorInvocation run(f.object,entry);
        check(run.resume(f.services).completed && f.field->vtable_word==0x80471384 && f.field->flag_34==0 && f.field->flag_35==0 && igb_bootstrap_read_byte(f.object,0x36)==0xcc,
            "Reference base entry omitted reset or overwrote padding.");
    }
    check(!igb_meta_field_kind_for_factory(0) && !igb_meta_field_kind_for_factory(0x8006388c),"Unknown factory identity was guessed.");
    rejects([]{igb_meta_field_profile(static_cast<IgbMetaFieldKind>(13));},"Invalid descriptor profile was accepted.");
}
void family_reset_prefixes(){
    for(const auto& p:evidence){
        Fixture f;resize(f,p.bytes);f.services->dependencies->globals->string_pool.reset();unsigned resets{};
        f.string_factory=[&](const IgbDependenciesCall& c){check(c.kind==S::create_string_pool,"Unexpected null-pool constructor operation.");++resets;
            if(resets==3 && p.kind==IgbMetaFieldKind::object)check(igb_bootstrap_read_word(f.object,0x40)==0 && igb_bootstrap_read_word(f.object,0x38)==0xcccccccc &&
                igb_bootstrap_read_byte(f.object,0x3c)==0xcc,"Object constructor failed to clear cache40 before its third reset or stored later fields early.");
            if(resets==3 && p.kind==IgbMetaFieldKind::memory_reference)check(igb_bootstrap_read_word(f.object,0x48)==0 && igb_bootstrap_read_word(f.object,0x4c)==0 &&
                igb_bootstrap_read_word(f.object,0x38)==0xcccccccc && igb_bootstrap_read_byte(f.object,0x42)==0xcc,
                "Memory-reference constructor changed its pre-reset ownership stores or published defaults early.");
            auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[]{return MessengerCallResult{true,0,{}};};return op;};
        IgbMetaFieldConstructorInvocation run(f.object,IgbMetaFieldConstructor::specialized,p.kind);
        check(run.resume(f.services).completed && resets==(ref_kind(p.kind)?3u:2u),"Specialized constructor skipped an original base reset pass.");
    }
    Fixture object;resize(object,0x40);IgbMetaFieldConstructorInvocation short_object(object.object,IgbMetaFieldConstructor::specialized,IgbMetaFieldKind::object);
    rejects([&]{short_object.resume(object.services);},"Object constructor accepted missing cache40 storage.");
    check(object.field->vtable_word==0x804762fc && object.field->flag_34==0 && object.field->flag_35==0 && !object.field->flag_3c &&
        igb_bootstrap_read_word(object.object,0x38)==0xcccccccc,"Object physical failure skipped original intermediate vtable or committed future defaults.");
    Fixture memory;resize(memory,0x4c);IgbMetaFieldConstructorInvocation short_memory(memory.object,IgbMetaFieldConstructor::specialized,IgbMetaFieldKind::memory_reference);
    rejects([&]{short_memory.resume(memory.services);},"Memory-reference constructor accepted missing word4C storage.");
    check(memory.field->vtable_word==0x80476088 && memory.field->word_48==0 && !memory.field->word_4c && !memory.field->flag_42 &&
        igb_bootstrap_read_word(memory.object,0x38)==0xcccccccc,"Memory-reference failure lost the completed word48 store or wrote future fields.");
    Fixture string;resize(string,0x38);IgbMetaFieldConstructorInvocation short_string(string.object,IgbMetaFieldConstructor::specialized,IgbMetaFieldKind::string);
    rejects([&]{short_string.resume(string.services);},"String constructor accepted missing flag38 storage.");
    check(string.field->vtable_word==0x8047693c && string.field->flag_35==0 && string.field->flag_27==0,"String failure committed later flag27/35 stores.");
}
void bind_tail_values(Fixture& f,unsigned& allocations,bool& ready){
    using V=IgbFieldOperationKind;f.services->fields=std::make_shared<IgbFieldOperationsServices>();
    f.services->fields->factory=[&f,&allocations,&ready](const IgbFieldOperationCall& c){auto op=std::make_shared<IgbFieldOperation>();
        if(c.kind==V::field_virtual && c.virtual_slot==0x8c){check(c.argument==0 && !c.source,"Post-constructor field default lost its null source.");
            auto prepare=std::make_shared<IgbFieldBufferInvocation>(c.field);auto services=f.services->fields;
            op->resume=[prepare,services]{return prepare->resume(services);};
        }else op->resume=[&f,&allocations,&ready,c,out=op.get()]{
            if(c.kind==V::field_virtual){check(c.virtual_slot==0x64,"Unexpected tail virtual.");return MessengerCallResult{true,4,{}};}
            if(c.kind==V::object_pool){check(c.handle==0 && igb_bootstrap_read_word(f.object,0x20)==0 && f.field->size==4 &&
                    igb_bootstrap_read_byte(f.object,0x15)==4,"Tail buffer size was not physically published before pool selection.");out->pool_result=f.pool;}
            else if(c.kind==V::allocate_default){++allocations;check(c.bytes==4 && c.alignment==1 && c.pool==f.pool,"Tail default allocation arguments changed.");
                if(!ready)return MessengerCallResult{false,0,"prepared tail allocator"};
                auto data=std::make_shared<IgbScratchBuffer>();data->bytes.assign(4,0xcd);data->initialized.assign(4,1);out->buffer_result={data,0};out->buffer_address=0x200000;}
            else throw std::runtime_error("Unexpected tail buffer operation");return MessengerCallResult{true,0,{}};};return op;};
}
void family_factory_tails(){
    for(const auto& p:evidence){
        Fixture f;resize(f,p.bytes);unsigned allocations{};bool ready=true;bind_tail_values(f,allocations,ready);
        IgbMetaFieldFactoryInvocation run(f.pool,p.kind);check(run.resume(f.services).completed && run.result()==f.field,"Concrete family factory did not complete.");
        check(f.raw.size()==1 && f.raw[0].bytes==p.bytes && f.field->vtable_word==p.vtable,"Family factory allocated another class's size or constructor.");
        const bool defaults=p.kind==IgbMetaFieldKind::string || p.kind==IgbMetaFieldKind::object;
        check(allocations==(defaults?1u:0u) && f.field->size==(defaults?4:0) && igb_bootstrap_read_word(f.object,0x20)==(defaults?0x200000u:0u),
            "Factory +2C tail was replaced with a generic leaf or allocated unrelated fields.");
    }
    Fixture f;resize(f,0x3c);unsigned allocations{};bool ready{};bind_tail_values(f,allocations,ready);
    IgbMetaFieldFactoryInvocation suspended(f.pool,IgbMetaFieldKind::string);
    check(!suspended.resume(f.services).completed && allocations==1 && f.field->vtable_word==0x8047693c && f.field->size==4 &&
        f.services->completed_allocations.empty() && igb_bootstrap_read_word(f.object,0x20)==0,"Tail allocation pause lost its completed constructor/size prefix.");
    ready=true;check(suspended.resume(f.services).completed && allocations==2 && f.raw.size()==1 && f.services->completed_allocations.size()==1,"Pending tail allocation restarted construction.");
    Fixture missing;resize(missing,0x44);IgbMetaFieldFactoryInvocation unbound(missing.pool,IgbMetaFieldKind::object);
    check(!unbound.resume(missing.services).completed && missing.field->vtable_word==0x804762fc && missing.field->size==0 &&
        missing.services->completed_allocations.empty(),"Missing object default virtual was reported as a completed leaf.");
}
void family_enabled_registration(){
    for(const auto& p:evidence){
        Fixture f;f.core->enabled=1;resize(f,p.bytes);auto initial=std::make_shared<IgbRuntimeMetaType>(),fresh=std::make_shared<IgbRuntimeMetaType>();
        const auto slot=p.kind==IgbMetaFieldKind::unsigned_int?f.services->metadata:
            (f.services->metadata_slots[static_cast<std::size_t>(p.kind)]=std::make_shared<IgbRegistrationMetaSlot>());
        *slot=initial;bool registered{};
        f.factory=[&](const IgbMetaFieldCall& c){auto op=std::make_shared<IgbMetaFieldOperation>();op->resume=[&,c,out=op.get()]{
            if(c.kind==K::register_type){check(c.function==p.registration,"Family enabled path selected another registration wrapper.");registered=true;*slot=fresh;}
            else{check(c.kind==K::reflected_factory && c.metadata==fresh && c.pool==f.pool && registered,"Family reflected path lost canonical profile metadata.");out->allocation_result=f.object;}
            return MessengerCallResult{true,0,{}};};return op;};
        IgbMetaFieldFactoryInvocation run(f.pool,p.kind);check(run.resume(f.services).completed && f.raw.empty() && f.calls.size()==2 &&
            igb_bootstrap_read_word(f.object,0)==0xcccccccc,"Enabled family path constructed or ran direct factory tails on its reflected result.");
    }
}
void always_reflected_profiles(IgbMetaFieldKind kind){
    const auto& p=igb_meta_field_profile(kind);
    if(kind==IgbMetaFieldKind::unsigned_byte)
        check(p.factory==0x8002310c && p.vtable==0x80470a44 && p.bytes==0x34 && p.registration==0x8002320c &&
            p.callback==0x80023234 && p.metadata_global==0x805614d0,"Unsigned-byte factory profile changed original identities.");
    else check(kind==IgbMetaFieldKind::structure && p.factory==0x8002450c && p.vtable==0x80470e00 && p.bytes==0x40 &&
        p.registration==0x8002460c && p.callback==0x80024634 && p.metadata_global==0x80561560,
        "Structure factory profile changed original identities.");
    check(p.constructor==0 && p.reset==0 && igb_meta_field_kind_for_factory(p.factory)==kind,
        "Always-reflected profile invented a specialized raw constructor or reset.");
    check(static_cast<unsigned>(IgbMetaFieldKind::unsigned_byte)==11 && static_cast<unsigned>(IgbMetaFieldKind::structure)==12,
        "Structure insertion shifted an existing factory profile.");
    for(unsigned state=0;state<6;++state){
        Fixture f;resize(f,p.bytes);auto old=std::make_shared<IgbRuntimeMetaType>(),fresh=std::make_shared<IgbRuntimeMetaType>();
        auto slot=std::make_shared<IgbRegistrationMetaSlot>(old);f.services->metadata_slots[static_cast<std::size_t>(kind)]=slot;
        if(state==0)f.services->bootstrap.reset();
        else if(state==1)f.services->bootstrap->runtime.reset();
        else if(state==2)f.services->bootstrap->runtime->singleton.reset();
        else if(state==3)f.services->bootstrap->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
        else f.core->enabled=state==5;
        f.factory=[&](const IgbMetaFieldCall& c){auto op=std::make_shared<IgbMetaFieldOperation>();
            op->resume=[&,c,out=op.get()]{
                if(c.kind==K::register_type){check(c.function==p.registration,"Factory skipped its unconditional registration wrapper.");*slot=fresh;}
                else{check(c.kind==K::reflected_factory && c.pool==f.pool && c.metadata==fresh && f.calls.size()==2,
                        "Always-reflected factory did not reload metadata after registration.");out->allocation_result=f.object;}
                return MessengerCallResult{true,0,{}};
            };return op;};
        IgbMetaFieldFactoryInvocation run(f.pool,kind);
        check(run.resume(f.services).completed && run.result()==f.field && !run.faulted() && f.raw.empty() && f.strings.empty(),
            "Always-reflected factory consulted ArkCore or ran a raw constructor.");
        check(f.calls.size()==2 && f.services->completed_allocations.size()==1 &&
            f.object.storage->bytes->bytes==std::vector<std::uint8_t>(p.bytes,0xcc),
            "Always-reflected reflected result ran direct tails or lost its physical allocation owner.");
        check(run.resume().completed && f.calls.size()==2 && f.services->completed_allocations.size()==1,
            "Completed always-reflected factory repeated registration, reflection or ownership publication.");
    }
    for(const auto entry:{IgbMetaFieldConstructor::specialized,IgbMetaFieldConstructor::specialized_reset}){
        Fixture f;resize(f,p.bytes);rejects([&]{IgbMetaFieldConstructorInvocation invalid(f.object,entry,kind);},
            "Always-reflected profile accepted an unsupported specialized raw constructor or reset.");
        check(f.object.storage->bytes->bytes==std::vector<std::uint8_t>(p.bytes,0xcc) && f.field->construction_active_.expired(),
            "Rejected always-reflected constructor changed storage or acquired a construction lease.");
    }
    for(const auto entry:{IgbMetaFieldConstructor::base,IgbMetaFieldConstructor::reset}){
        Fixture f;resize(f,p.bytes);IgbMetaFieldConstructorInvocation base(f.object,entry,kind);
        check(base.resume(f.services).completed && base.result()==f.field && f.field->vtable_word==0x80471914 &&
            f.field->reference_word==1 && igb_bootstrap_read_byte(f.object,0x16)==0xcc && igb_bootstrap_read_byte(f.object,0x2f)==0xcc,
            "Always-reflected profile prevented the actual shared base entry or published a specialized table.");
    }
}
void retained_reflected_profiles(IgbMetaFieldKind kind){
    const auto& p=igb_meta_field_profile(kind);const auto index=static_cast<std::size_t>(kind);
    Fixture f;resize(f,p.bytes);f.services->bootstrap.reset();auto initial=std::make_shared<IgbRuntimeMetaType>(),fresh=std::make_shared<IgbRuntimeMetaType>();
    auto slot=std::make_shared<IgbRegistrationMetaSlot>(initial);f.services->metadata_slots[index]=slot;
    bool registration_ready{},reflection_ready{};unsigned registration_polls{},reflection_polls{};
    std::shared_ptr<IgbMetaFieldOperation> selected_registration,selected_reflection;
    f.factory=[&](const IgbMetaFieldCall& c){auto op=std::make_shared<IgbMetaFieldOperation>();
        if(c.kind==K::register_type){check(c.function==p.registration,"Retained registration selected another wrapper.");selected_registration=op;
            op->resume=[&]{++registration_polls;if(!registration_ready)return MessengerCallResult{false,17,"Prepared always-reflected registration."};
                *slot=fresh;return MessengerCallResult{true,0,{}};};
        }else{check(c.kind==K::reflected_factory && c.metadata==fresh && c.pool==f.pool,"Always-reflected reflection lost captured metadata or pool.");selected_reflection=op;
            op->resume=[&,out=op.get()]{++reflection_polls;if(!reflection_ready)return MessengerCallResult{false,23,"Prepared always-reflected reflection."};
                out->allocation_result=f.object;return MessengerCallResult{true,0,{}};};
        }return op;};
    IgbMetaFieldFactoryInvocation run(f.pool,kind);const auto first=run.resume(f.services);
    check(!first.completed && first.value==17 && registration_polls==1 && reflection_polls==0 && !run.completed() && !run.faulted(),
        "Always-reflected factory lost its pending registration status or selected reflection early.");
    rejects([&]{run.result();},"Pending always-reflected factory exposed a result.");
    rejects([&]{run.allocation();},"Pending always-reflected factory exposed an allocation.");
    selected_registration->resume=[]()->MessengerCallResult{throw std::runtime_error("Replaced registration continuation was used.");};
    registration_ready=true;const auto second=run.resume(f.services);
    check(!second.completed && second.value==23 && registration_polls==2 && reflection_polls==1 && f.calls.size()==2,
        "Always-reflected resume replayed registration selection or lost its reflection status.");
    *slot=initial;f.services->factory={};selected_reflection->resume=[]()->MessengerCallResult{throw std::runtime_error("Replaced reflection continuation was used.");};
    reflection_ready=true;check(run.resume(f.services).completed && run.result()==f.field && registration_polls==2 && reflection_polls==2 &&
        f.raw.empty() && f.calls.size()==2,"Always-reflected reflection reloaded an already captured cell or replaced its retained callback.");

    Fixture late;late.services->bootstrap.reset();unsigned registrations{};
    late.factory=[&](const IgbMetaFieldCall& c){auto op=std::make_shared<IgbMetaFieldOperation>();
        op->resume=[&,c]{if(c.kind==K::register_type){++registrations;check(c.function==p.registration,"Late metadata selected another wrapper.");}
            else check(c.kind==K::reflected_factory && !c.metadata,"Represented-null always-reflected metadata was not forwarded to reflection.");
            return MessengerCallResult{true,0,{}};};return op;};
    IgbMetaFieldFactoryInvocation absent(late.pool,kind);
    check(!absent.resume(late.services).completed && !absent.resume(late.services).completed && registrations==1 && late.calls.size()==1,
        "Missing always-reflected metadata became a null reflected allocation or replayed registration.");
    late.services->metadata_slots[index]=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    check(absent.resume(late.services).completed && !absent.result() && !absent.allocation().storage && late.raw.empty() && late.calls.size()==2,
        "Null always-reflected reflected result reached direct factory tails or invented an allocation.");
}
void reflected_profile_callback_and_fault(IgbMetaFieldKind kind){
    const auto& p=igb_meta_field_profile(kind);
    Fixture f;f.services->bootstrap.reset();f.services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();
    auto fresh=std::make_shared<IgbRuntimeMetaType>();unsigned bodies{};
    f.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){
        check(c.kind==IgbRegistrationCallbacksServiceKind::invoke_function && c.function==p.callback,
            "Always-reflected factory callback runner selected another original body.");
        auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++bodies;
            f.services->metadata_slots[static_cast<std::size_t>(kind)]=std::make_shared<IgbRegistrationMetaSlot>(fresh);
            return MessengerCallResult{true,0,{}};};return op;};
    f.factory=[&](const IgbMetaFieldCall& c){check(c.kind==K::reflected_factory && c.metadata==fresh,"Always-reflected callback path repeated leaf registration.");
        auto op=std::make_shared<IgbMetaFieldOperation>();op->resume=[]()->MessengerCallResult{throw std::runtime_error("Prepared reflected failure.");};return op;};
    IgbMetaFieldFactoryInvocation run(f.pool,kind);
    rejects([&]{run.resume(f.services);},"Always-reflected reflected failure was swallowed.");
    check(run.faulted() && !run.completed() && bodies==1 && f.calls.size()==1 && f.raw.empty() && f.services->completed_allocations.empty(),
        "Faulted always-reflected reflection published a result or ran a raw fallback.");
    rejects([&]{run.resume(f.services);},"Faulted always-reflected factory restarted.");
    rejects([&]{run.result();},"Faulted always-reflected factory exposed a result.");
}
void native_reflected_adapter(){
    // Explicit empty class isolates adapter/type ownership. Actual object-field
    // defaults and registration are exercised in the cold-bootstrap fixture.
    auto reflected=std::make_shared<IgbFileFactoryServices>();
    auto metadata=std::make_shared<IgbRuntimeMetaType>();metadata->factory_redirect=0;
    metadata->allocation_prefix=8;metadata->native_size=0x44;metadata->type_word=0x804762fc;
    metadata->instance_count=0;metadata->instances=std::shared_ptr<IgbObjectInstanceList>{};
    metadata->fields=std::make_shared<IgbDecodeFieldRegistry>();metadata->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
    auto base=std::make_shared<IgbRuntimeMetaType>();base->allocation_prefix=0;
    base->fields=std::make_shared<IgbDecodeFieldRegistry>();base->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
    auto field=std::make_shared<IgbDecodeRuntimeField>();auto pointer=allocation(0x300000,0x4c);
    pointer.storage->bindings.push_back({8,{},{},{},{},field});
    auto pool=std::make_shared<ModelSceneData>();auto core=std::make_shared<IgbRuntimeTypeRegistry>();
    core->enabled=1;core->field_initialization_enabled=1;core->vtable_offset=0;
    reflected->initializer=std::make_shared<IgbObjectInitServices>();reflected->initializer->runtime=std::make_shared<IgbRuntimeTypesServices>();
    reflected->initializer->runtime->singleton=core;reflected->initializer->object_metadata=base;
    reflected->initializer->factory=[metadata](const IgbObjectInitCall& call){auto op=std::make_shared<IgbObjectInitOperation>();
        op->resume=[metadata,call,out=op.get()]{if(call.kind==IgbObjectInitServiceKind::object_virtual && call.virtual_slot==0x58)out->metadata_result=metadata;
            return MessengerCallResult{true,0,{}};};return op;};
    unsigned selected{},polls{};bool ready{};
    reflected->factory=[&](const IgbFileFactoryCall& call){check(call.kind==IgbFileFactoryServiceKind::allocate && call.pool==pool && call.bytes==0x4c,
        "Descriptor adapter changed reflected allocation size or pool.");++selected;auto op=std::make_shared<IgbFileFactoryOperation>();
        op->resume=[&,out=op.get()]{++polls;if(!ready)return MessengerCallResult{false,0,"Prepared descriptor allocation."};
            out->allocation_result=pointer.storage;return MessengerCallResult{true,0,{}};};return op;};
    unsigned remaining{};const auto adapter=make_native_igb_reflected_meta_field_factory(reflected,[&](const IgbMetaFieldCall&){++remaining;return std::shared_ptr<IgbMetaFieldOperation>{};});
    IgbMetaFieldCall request;request.kind=K::register_type;check(!adapter(request) && remaining==1,"Reflected adapter consumed a registration endpoint.");
    request.kind=K::reflected_factory;request.metadata=metadata;request.pool=pool;auto op=adapter(request);
    check(op && !op->resume().completed && selected==1,"Native descriptor adapter bypassed pending reflected allocation.");
    reflected->factory={};ready=true;check(op->resume().completed && selected==1 && polls==2,"Descriptor adapter replaced a pending reflected allocator.");
    check(op->allocation_result.storage==pointer.storage && op->allocation_result.offset==8 && igb_bootstrap_field(op->allocation_result)==field,
        "Descriptor adapter lost adjusted storage or its exact field owner.");
    check(igb_bootstrap_read_word(op->allocation_result,0)==0x804762fc && metadata->instance_count==1,
        "Descriptor adapter skipped actual object initialization or instance publication.");
    auto abstract=std::make_shared<IgbRuntimeMetaType>();abstract->factory_redirect=0;abstract->flag_1a=1;request.metadata=abstract;
    auto empty=adapter(request);check(empty->resume().completed && !empty->allocation_result.storage && selected==1,"Abstract descriptor result was fabricated or allocated.");
    auto temporary=std::make_shared<IgbFileFactoryServices>();auto expired=make_native_igb_reflected_meta_field_factory(temporary);temporary.reset();
    rejects([&]{expired(request);},"Expired descriptor services were treated as a successful factory.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_meta_field_factory_tests");
    exact_constructed_bytes();double_reset_and_pending();injected_name_release_and_owners();enabled_branch_and_faults();
    family_constructor_bytes();family_reset_prefixes();family_factory_tails();family_enabled_registration();native_reflected_adapter();
    for(const auto kind:{IgbMetaFieldKind::unsigned_byte,IgbMetaFieldKind::structure}){
        always_reflected_profiles(kind);retained_reflected_profiles(kind);reflected_profile_callback_and_fault(kind);}
    std::cout<<"igb_meta_field_factory_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
