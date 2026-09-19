#include "dw4/igb_meta_field_factory.hpp"

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
        check(f.field->declaring_type && !*f.field->declaring_type && f.field->global_slot && igb_field_global_slot_is_null(*f.field->global_slot) && f.field->global_slot_address==0,"Descriptor reset failed to establish known null pointer slots.");
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
    IgbMetaFieldConstructorInvocation overwrite(nonowning.object,IgbMetaFieldConstructor::reset);
    check(overwrite.resume(nonowning.services).completed && igb_field_global_slot_is_null(*nonowning.field->global_slot),"Raw constructor reset dereferenced or retained a replaced nonowning global cell.");
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
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_meta_field_factory_tests");
    exact_constructed_bytes();double_reset_and_pending();injected_name_release_and_owners();enabled_branch_and_faults();
    std::cout<<"igb_meta_field_factory_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
