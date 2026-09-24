#include "dw4/igb_field_probe.hpp"

#include <iostream>

namespace {
using namespace dw4;using K=IgbDependenciesServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<ModelSceneObject> pool(){return std::make_shared<ModelSceneData>();}
std::shared_ptr<IgbInternedString> string(std::uint32_t count){auto p=std::make_shared<IgbInternedString>();
    p->references=count;p->container=pool();p->value=std::make_shared<const std::string>("probe");return p;}
struct Fixture {
    std::shared_ptr<IgbFieldProbeServices> services=std::make_shared<IgbFieldProbeServices>();
    std::shared_ptr<IgbDecodeRuntimeField> field=std::make_shared<IgbDecodeRuntimeField>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    IgbBootstrapPointer local;std::vector<IgbDependenciesCall> calls;
    std::function<void(const IgbDependenciesCall&,MessengerCallResult&)> action;
    Fixture(){
        local.storage=std::make_shared<IgbObjectStorage>();local.storage->original_address=0x900008;
        local.storage->bytes=std::make_shared<IgbScratchBuffer>();local.storage->bytes->bytes.assign(0x44,0xcc);
        local.storage->bytes->initialized.assign(0x44,1);local.storage->bindings.push_back({0,{},{},{},{},field});
        services->fields=std::make_shared<IgbMetaFieldFactoryServices>();
        services->fields->bootstrap=std::make_shared<IgbBootstrapServices>();
        services->fields->dependencies=std::make_shared<IgbDependenciesServices>();
        services->fields->dependencies->globals=std::make_shared<SystemStageServices>();
        services->fields->dependencies->globals->string_pool=pool();
        services->runtime=std::make_shared<IgbRuntimeTypesServices>();services->runtime->singleton=core;core->vtable_offset=0;
        services->local_storage=[this]{return local;};
        services->fields->dependencies->factory=[this](const IgbDependenciesCall& c){
            auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[this,c,out=op.get()]{calls.push_back(c);
                if(c.kind==K::create_string_pool || c.kind==K::initialize_string_pool)out->pool_result=pool();
                MessengerCallResult result{true,0,{}};if(action)action(c,result);return result;};return op;};
    }
    void pause(IgbFieldProbeInvocation& call){services->runtime->singleton.reset();
        check(!call.resume(services).completed,"Missing core did not retain the completed reset.");
        check(field->vtable_word==0x804762fc && field->reference_word==1 && field->word_40==0 && field->flag_3c==1,
            "Probe did not execute the actual object-field reset.");services->runtime->singleton=core;}
    void cache(const std::shared_ptr<IgbInternedString>& header,std::uint32_t address){
        igb_bootstrap_write_word(local,0x40,address);services->strings[address]=header;}
    void memory(){local.storage->bytes->bytes.resize(0x50,0xcc);local.storage->bytes->initialized.resize(0x50,1);
        services->object_cleanup=std::make_shared<IgbObjectFieldConstructServices>();}
    void pause_memory(IgbFieldProbeInvocation& call){services->runtime->singleton.reset();
        check(!call.resume(services).completed,"Memory probe did not pause after reset.");
        check(field->vtable_word==0x80476088 && field->reference_word==1 && field->word_48==0 && field->word_4c==0,
            "Memory probe substituted another class reset.");services->runtime->singleton=core;}
};
IgbObjectInstance object(std::uint32_t address,std::uint32_t references){
    IgbObjectInstance result;result.offset=8;result.field=std::make_shared<IgbDecodeRuntimeField>();
    result.storage=std::make_shared<IgbObjectStorage>();result.storage->original_address=address-8u;
    result.storage->bytes=std::make_shared<IgbScratchBuffer>();result.storage->bytes->bytes.assign(0x18,0xa5);
    result.storage->bytes->initialized.assign(0x18,1);result.storage->bindings.push_back({8,{},{},{},{},result.field});
    igb_object_set_reference_word(result,references);return result;
}
void reset_and_capture(){
    for(const auto offset:{0u,4u,8u,0x10u,0x20u,0x24u,0x34u,0x38u,0x40u}){Fixture f;f.core->vtable_offset=offset;IgbFieldProbeInvocation call(f.local);
        check(call.resume(f.services).completed,"Object field probe did not complete.");
        check(call.value()==(offset==0?0x804762fcu:igb_bootstrap_read_word(f.local,offset)),"Probe substituted a constant for its physical word.");
        check(f.field->vtable_word==0x80471914 && f.calls.empty() && f.field->size==0,"Probe cleanup or reset invoked unrelated factory tails.");
        check(call.resume().completed,"Completed probe did not retain its return value.");}
    Fixture f;f.core->vtable_offset.reset();IgbFieldProbeInvocation call(f.local);check(!call.resume(f.services).completed,"Unknown core offset was fabricated.");
    auto changed=std::make_shared<IgbRuntimeTypeRegistry>();changed->vtable_offset=4;f.services->runtime->singleton=changed;f.core->vtable_offset=0;
    igb_bootstrap_write_word(f.local,0,0x12345678);
    check(call.resume(f.services).completed && call.value()==0x12345678,"Probe repeated reset or reloaded a captured core.");
    Fixture live;live.services->fields->dependencies->globals->string_pool.reset();
    live.action=[&](const auto& c,auto&){if(c.kind==K::initialize_string_pool)live.core->vtable_offset=4;};
    IgbFieldProbeInvocation late(live.local);check(late.resume(live.services).completed && late.value()==1 && live.calls.size()==2,
        "Probe selected core state before reset's pool callbacks.");
}
void cleanup_order(){
    Fixture f;IgbFieldProbeInvocation call(f.local);f.pause(call);auto cache=string(1),name=string(8),fresh=string(1);bool ready{};unsigned releases{};
    f.cache(cache,0x1008);igb_meta_field_set_name(f.local,name,0x2008);igb_bootstrap_write_word(f.local,0,0x12345678);
    f.action=[&](const auto& c,auto& result){check(c.kind==K::release_string,"Unexpected probe operation.");++releases;
        if(c.string==cache){check(f.field->vtable_word==0x804762fc && cache->references==0 && c.pool==cache->container,"Cache release lost prefix or captured pool.");
            igb_meta_field_set_name(f.local,fresh,0x3008);result.completed=ready;}
        else check(c.string==fresh && f.field->vtable_word==0x80471914 && fresh->references==0,"Name cleanup did not observe live name and base vtable.");};
    check(!call.resume(f.services).completed && releases==1 && fresh->references==1,"Pending cache release ran later cleanup.");
    ready=true;check(call.resume(f.services).completed && call.value()==0x12345678 && releases==3,"Cleanup suspension replayed the probe or skipped fresh name.");
    check(cache->references==0 && name->references==8 && fresh->references==0 && igb_bootstrap_read_word(f.local,0x40)==0x1008 &&
        igb_bootstrap_read_word(f.local,0x0c)==0x3008,"Cleanup fabricated null stores or additional reference operations.");
    Fixture wrapping;IgbFieldProbeInvocation wrap(wrapping.local);wrapping.pause(wrap);auto zero=string(0),tagged=string(0x800001);
    wrapping.cache(zero,0x4008);igb_meta_field_set_name(wrapping.local,tagged,0x5008);
    check(wrap.resume(wrapping.services).completed && zero->references==0xffffffffu && tagged->references==0x800000 && wrapping.calls.empty(),
        "String release used packed object counts or failed full-word wrap.");
}
void missing_bindings_and_failures(){
    Fixture f;IgbFieldProbeInvocation call(f.local);f.pause(call);igb_bootstrap_write_word(f.local,0x40,0x6008);
    check(!call.resume(f.services).completed && f.field->vtable_word==0x804762fc,"Unknown cache header skipped a reached release.");
    auto replacement=string(3),captured=string(2);f.cache(replacement,0x7008);f.services->strings[0x6008]=captured;
    check(call.resume(f.services).completed && replacement->references==3 && captured->references==1,"Late header binding redirected the captured pointer.");
    for(const auto offset:{1u,0x44u,0xfffffffcu}){Fixture invalid;IgbFieldProbeInvocation run(invalid.local);invalid.pause(run);
        invalid.core->vtable_offset=offset;igb_bootstrap_write_word(invalid.local,0,0x11223344);
        rejects([&]{run.resume(invalid.services);},"Out-of-profile local read succeeded.");
        check(run.faulted() && igb_bootstrap_read_word(invalid.local,0)==0x11223344,"Read failure performed later destructor stores.");}
    Fixture null;null.services->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};IgbFieldProbeInvocation missing(null.local);
    rejects([&]{missing.resume(null.services);},"Null core was treated as an unknown binding.");check(null.field->word_40==0,"Null-core failure skipped reset.");
    Fixture padding;padding.core->vtable_offset=0x3c;padding.local.storage->bytes->initialized[0x3f]=0;IgbFieldProbeInvocation unreadable(padding.local);
    rejects([&]{unreadable.resume(padding.services);},"Uninitialized local padding was guessed.");
    Fixture dead;IgbFieldProbeInvocation bad(dead.local);dead.pause(bad);auto released=string(1);released->released=true;dead.cache(released,0x8008);
    rejects([&]{bad.resume(dead.services);},"Released string header was read.");check(released->references==1,"Fault changed a released string count.");
    Fixture recursive;IgbFieldProbeInvocation reentry(recursive.local);recursive.pause(reentry);recursive.cache(string(1),0x9008);
    recursive.action=[&](const auto&,auto&){rejects([&]{reentry.resume(recursive.services);},"Recursive probe was accepted.");};
    rejects([&]{reentry.resume(recursive.services);},"Caught reentry allowed outer completion.");check(reentry.faulted(),"Recursive probe did not retain its fault.");
}
void adapters(){
    Fixture f;unsigned allocations{};f.services->local_storage=[&]{++allocations;return f.local;};auto adapter=make_native_igb_field_probe_factory(f.services);
    IgbTypeRegistrationCall request;request.kind=IgbTypeRegistrationServiceKind::defaults;request.function=0x80028934;
    check(!adapter(request),"Probe adapter accepted a defaults operation.");request.kind=IgbTypeRegistrationServiceKind::layout_probe;
    auto op=adapter(request);f.services->runtime->singleton.reset();check(op && !op->resume().completed && allocations==1,"Probe adapter lost its selected local.");
    f.services->runtime->singleton=f.core;check(op->resume().completed && allocations==1,"Probe adapter recreated local storage.");
    std::weak_ptr<IgbObjectStorage> retained=f.local.storage;op.reset();f.local={};check(!retained.expired(),"Probe discarded retained guest ownership.");
    Fixture late;late.services->local_storage={};auto wait=make_native_igb_field_probe_factory(late.services)(request);
    check(!wait->resume().completed,"Missing stack representation was fabricated.");late.services->local_storage=[&]{return late.local;};
    check(wait->resume().completed,"Late stack storage failed to resume.");
}
void signed_int_probe(){
    for(const auto offset:{0u,4u,0x10u,0x24u,0x30u}){Fixture f;
        f.local.storage->bytes->bytes.resize(0x34);f.local.storage->bytes->initialized.resize(0x34);f.core->vtable_offset=offset;
        IgbFieldProbeInvocation run(f.local,IgbMetaFieldKind::signed_int);
        check(run.resume(f.services).completed && run.value()==(offset==0?0x80475b60u:igb_bootstrap_read_word(f.local,offset)),
            "Integer probe substituted another class reset or failed exact short storage.");
        check(f.field->vtable_word==0x80471914 && f.calls.empty(),"Integer probe executed object-cache or reference-base cleanup.");
    }
    Fixture f;f.services->runtime->singleton.reset();IgbFieldProbeInvocation suspended(f.local,IgbMetaFieldKind::signed_int);
    check(!suspended.resume(f.services).completed && f.field->vtable_word==0x80475b60,"Integer probe did not pause after its real reset.");
    auto name=string(1);igb_meta_field_set_name(f.local,name,0x1008);f.services->runtime->singleton=f.core;bool ready{};unsigned calls{};
    f.action=[&](const IgbDependenciesCall& c,MessengerCallResult& status){++calls;check(c.kind==K::release_string && c.string==name && name->references==0 &&
        f.field->vtable_word==0x80471914,"Integer cleanup changed full-word release or vtable order.");status.completed=ready;};
    check(!suspended.resume(f.services).completed && calls==1,"Integer name release did not retain its pending continuation.");ready=true;
    check(suspended.resume(f.services).completed && calls==2 && name->references==0 && suspended.value()==0x80475b60,
        "Integer release resume repeated the decrement or selected-word read.");
    Fixture invalid;invalid.core->vtable_offset=0x34;IgbFieldProbeInvocation outside(invalid.local,IgbMetaFieldKind::signed_int);
    rejects([&]{outside.resume(invalid.services);},"Integer probe accepted bytes outside its original0x34-byte object.");
    check(invalid.field->vtable_word==0x80475b60,"Integer bounds fault ran cleanup prematurely.");
    Fixture selected;unsigned locals{};selected.services->profile_local_storage=[&](IgbMetaFieldKind kind){++locals;
        check(kind==IgbMetaFieldKind::signed_int,"Integer adapter selected another stack profile.");return selected.local;};
    IgbTypeRegistrationCall request;request.kind=IgbTypeRegistrationServiceKind::layout_probe;request.function=0x8002e308;
    auto op=make_native_igb_field_probe_factory(selected.services)(request);check(op && op->resume().completed && locals==1,"Integer probe adapter was not composed.");
}
void memory_reset_and_capture(){
    for(std::uint32_t offset=0;offset<0x50;offset+=4){Fixture f;f.memory();f.core->vtable_offset=offset;
        IgbFieldProbeInvocation run(f.local,IgbMetaFieldKind::memory_reference);
        check(run.resume(f.services).completed,"Memory probe failed its actual reset and null-member cleanup.");
        check(run.value()==(offset==0?0x80476088u:igb_bootstrap_read_word(f.local,offset)),"Memory probe did not capture the selected physical word.");
        check(f.field->vtable_word==0x80471914 && f.calls.empty() && f.field->word_48==0 && f.field->word_4c==0,
            "Memory reset or null cleanup introduced a reached release.");
        check(igb_bootstrap_read_word(f.local,0x40)==0x010101cc && igb_bootstrap_read_word(f.local,0x44)==0xffffffffu,
            "Memory reset overwrote padding or omitted its specialized stores.");
    }
    Fixture f;f.memory();f.core->vtable_offset.reset();IgbFieldProbeInvocation call(f.local,IgbMetaFieldKind::memory_reference);
    check(!call.resume(f.services).completed,"Unknown memory probe offset was fabricated.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=4;f.services->runtime->singleton=replacement;
    f.core->vtable_offset=0x44;igb_bootstrap_write_word(f.local,0x44,0x12345678);
    check(call.resume(f.services).completed && call.value()==0x12345678,"Memory probe reloaded its captured core or repeated reset.");
    Fixture late;late.memory();late.services->fields->dependencies->globals->string_pool.reset();
    late.action=[&](const auto& c,auto&){if(c.kind==K::initialize_string_pool)late.core->vtable_offset=4;};
    IgbFieldProbeInvocation fresh(late.local,IgbMetaFieldKind::memory_reference);
    check(fresh.resume(late.services).completed && fresh.value()==1 && late.calls.size()==2,"Memory probe captured core before reset callbacks.");
}
void memory_cleanup_order(){
    Fixture f;f.memory();auto run=std::make_unique<IgbFieldProbeInvocation>(f.local,IgbMetaFieldKind::memory_reference);f.pause_memory(*run);
    auto cache=string(1),name=string(8),fresh=string(1);auto first=object(0xa008,1),second=object(0xb008,0x800001);
    const auto second_field=second.field;std::weak_ptr<IgbObjectStorage> retained=second.storage;
    igb_bind_object_field_instance(f.services->object_cleanup,first);igb_bind_object_field_instance(f.services->object_cleanup,second);
    igb_bootstrap_write_word(f.local,0x4c,0x1008);f.services->strings[0x1008]=cache;
    igb_bootstrap_write_word(f.local,0x48,0xa008);igb_meta_field_set_name(f.local,name,0x2008);
    igb_bootstrap_write_word(f.local,0,0x12345678);bool cache_ready{},object_ready{};unsigned destroys{},selections{};
    std::vector<unsigned> events;
    f.action=[&](const IgbDependenciesCall& c,MessengerCallResult& status){
        check(c.kind==K::release_string,"Memory probe reached an unrelated dependency.");
        if(c.string==cache){events.push_back(1);check(cache->references==0 && c.pool==cache->container && f.field->vtable_word==0x80476088,
                "Memory cache release lost its prefix or captured pool.");
            igb_bootstrap_write_word(f.local,0x48,0xb008);status.completed=cache_ready;
        }else {events.push_back(3);check(c.string==fresh && fresh->references==0 && f.field->vtable_word==0x80471914,
                "Memory name cleanup preceded the object callback or missed the live name.");}
    };
    f.services->object_cleanup->factory=[&](const IgbObjectFieldConstructCall& c){++selections;
        check(c.kind==IgbObjectFieldConstructServiceKind::destroy_object && c.object.field==second_field,"Memory probe selected the stale +48 object.");
        auto op=std::make_shared<IgbObjectFieldConstructOperation>();op->resume=[&,captured=c.object]{++destroys;events.push_back(2);
            check(igb_object_reference_word(captured)==0x800000 && second_field->reference_word==0x800000 &&
                first.field->reference_word==1 && f.field->vtable_word==0x80476088,"Packed object release changed its count or order.");
            igb_meta_field_set_name(f.local,fresh,0x3008);return MessengerCallResult{object_ready,0,{}};};return op;
    };
    check(!run->resume(f.services).completed && destroys==0 && name->references==8,"Pending memory cache release advanced object cleanup.");
    cache_ready=true;check(!run->resume(f.services).completed && destroys==1 && fresh->references==1,"Pending object destruction advanced name cleanup.");
    f.services->object_cleanup->objects.clear();f.services->object_cleanup->factory={};second={};object_ready=true;
    check(run->resume(f.services).completed && selections==1 && destroys==2 && run->value()==0x12345678,
        "Memory resume lost the captured operation or repeated the selected-word read.");
    check(events==std::vector<unsigned>({1,1,2,2,3}) && cache->references==0 && name->references==8 && fresh->references==0,
        "Memory cleanup repeated a decrement or reordered callbacks.");
    check(igb_bootstrap_read_word(f.local,0x48)==0xb008 && igb_bootstrap_read_word(f.local,0x4c)==0x1008 &&
        igb_bootstrap_read_word(f.local,0x0c)==0x3008,"Memory cleanup fabricated member-null stores.");
    run.reset();check(!retained.expired(),"Memory cleanup lost represented ownership after address-map removal.");
}
void memory_packed_counts_and_bindings(){
    for(const auto count:{0u,1u,0x800000u,0x800001u,0xffffffffu}){Fixture f;f.memory();
        IgbFieldProbeInvocation run(f.local,IgbMetaFieldKind::memory_reference);f.pause_memory(run);
        auto held=object(0xa008,count);held.field->reference_word=0x12345678;
        igb_bind_object_field_instance(f.services->object_cleanup,held);igb_bootstrap_write_word(f.local,0x48,0xa008);unsigned destroys{};
        f.services->object_cleanup->factory=[&](const IgbObjectFieldConstructCall& c){
            check(c.kind==IgbObjectFieldConstructServiceKind::destroy_object,"Packed release selected a pool callback.");
            auto op=std::make_shared<IgbObjectFieldConstructOperation>();op->resume=[&]{++destroys;return MessengerCallResult{true,0,{}};};return op;};
        const auto expected=count-1u;
        check(run.resume(f.services).completed && igb_object_reference_word(held)==expected && held.field->reference_word==expected,
            "Memory probe used a stale canonical count or failed physical full-word decrement.");
        check(destroys==unsigned((expected&0x7fffffu)==0) && held.storage->bytes->bytes[0]==0xa5 && held.storage->bytes->bytes[0x10]==0xa5,
            "Memory probe masked the decrement or wrote outside the adjusted object header.");
    }
    Fixture f;f.memory();IgbFieldProbeInvocation run(f.local,IgbMetaFieldKind::memory_reference);f.pause_memory(run);
    igb_bootstrap_write_word(f.local,0x48,0xa008);
    check(!run.resume(f.services).completed && f.field->vtable_word==0x80476088,"Missing object binding skipped a reached reference read.");
    auto captured=object(0xa008,2),replacement=object(0xb008,5);igb_bootstrap_write_word(f.local,0x48,0xb008);
    igb_bind_object_field_instance(f.services->object_cleanup,captured);igb_bind_object_field_instance(f.services->object_cleanup,replacement);
    check(run.resume(f.services).completed && captured.field->reference_word==1 && replacement.field->reference_word==5,
        "Late object binding redirected an already captured pointer.");
    Fixture self;self.memory();IgbFieldProbeInvocation alias(self.local,IgbMetaFieldKind::memory_reference);self.pause_memory(alias);
    igb_bootstrap_write_word(self.local,4,2);self.core->vtable_offset=4;igb_bootstrap_write_word(self.local,0x48,igb_bootstrap_address(self.local));
    check(alias.resume(self.services).completed && alias.value()==2 && self.field->reference_word==1,
        "Explicit local alias lost the captured word or canonical reference identity.");
}
void memory_pending_destructor(){
    Fixture f;f.memory();IgbFieldProbeInvocation run(f.local,IgbMetaFieldKind::memory_reference);f.pause_memory(run);
    auto held=object(0xa008,1);igb_bind_object_field_instance(f.services->object_cleanup,held);igb_bootstrap_write_word(f.local,0x48,0xa008);
    check(!run.resume(f.services).completed && held.field->reference_word==0 && f.field->vtable_word==0x80476088,
        "Missing destructor endpoint skipped or repeated the reference prefix.");
    unsigned selections{},calls{};bool bind{},ready{};auto operation=std::make_shared<IgbObjectFieldConstructOperation>();
    f.services->object_cleanup->factory=[&](const IgbObjectFieldConstructCall& c){++selections;
        check(c.object.field==held.field && held.field->reference_word==0,"Late destructor factory changed captured owner or reference count.");
        return bind?operation:nullptr;};
    check(!run.resume(f.services).completed && selections==1,"Unbound destructor operation was fabricated.");bind=true;
    check(!run.resume(f.services).completed && selections==2,"Missing destructor continuation was fabricated.");
    operation->resume=[&]{++calls;return MessengerCallResult{ready,0,{}};};
    check(!run.resume(f.services).completed && selections==2 && calls==1,"Late destructor continuation was not retained.");
    operation->resume=[]()->MessengerCallResult{throw std::runtime_error("Retained callback was replaced.");};
    f.services->object_cleanup.reset();ready=true;
    check(run.resume(f.services).completed && held.field->reference_word==0 && selections==2 && calls==2,
        "Pending destruction reselected the callback or repeated the decrement.");
}
void memory_failures_and_adapter(){
    for(const auto mode:{0u,1u,2u,3u}){Fixture f;f.memory();IgbFieldProbeInvocation run(f.local,IgbMetaFieldKind::memory_reference);f.pause_memory(run);
        auto held=object(0xa008,1);igb_bind_object_field_instance(f.services->object_cleanup,held);igb_bootstrap_write_word(f.local,0x48,0xa008);
        auto name=string(2);igb_meta_field_set_name(f.local,name,0x2008);
        if(mode==0)f.services->object_cleanup->objects.push_back(object(0xa008,1));
        if(mode==1)held.storage->bytes->initialized[held.offset+4]=0;
        if(mode==2)held.storage->bytes->released=true;
        if(mode==3)f.services->object_cleanup->factory=[&](const auto&){auto op=std::make_shared<IgbObjectFieldConstructOperation>();
            op->resume=[]()->MessengerCallResult{throw std::runtime_error("Reached destructor failure.");};return op;};
        rejects([&]{run.resume(f.services);},"Invalid memory object access or destructor unexpectedly completed.");
        check(run.faulted() && held.field->reference_word==(mode==3?0u:1u) && name->references==2 && f.field->vtable_word==0x80476088,
            "Memory object failure changed its reached prefix or performed later cleanup.");
    }
    Fixture invalid;invalid.memory();invalid.local.storage->bytes->initialized[0x43]=0;invalid.core->vtable_offset=0x40;
    IgbFieldProbeInvocation padding(invalid.local,IgbMetaFieldKind::memory_reference);
    rejects([&]{padding.resume(invalid.services);},"Memory reset fabricated initialized padding for the selected word.");
    check(invalid.field->vtable_word==0x80476088,"Memory padding fault ran base cleanup.");
    Fixture recursive;recursive.memory();IgbFieldProbeInvocation reentry(recursive.local,IgbMetaFieldKind::memory_reference);recursive.pause_memory(reentry);
    auto held=object(0xa008,1);igb_bind_object_field_instance(recursive.services->object_cleanup,held);igb_bootstrap_write_word(recursive.local,0x48,0xa008);
    recursive.services->object_cleanup->factory=[&](const auto&){auto op=std::make_shared<IgbObjectFieldConstructOperation>();op->resume=[&]{
        rejects([&]{reentry.resume(recursive.services);},"Recursive memory probe was accepted.");return MessengerCallResult{true,0,{}};};return op;};
    rejects([&]{reentry.resume(recursive.services);},"Swallowed object callback reentry allowed outer completion.");
    check(reentry.faulted() && held.field->reference_word==0 && recursive.field->vtable_word==0x80476088,"Recursive failure lost its object release prefix.");
    Fixture f;f.memory();unsigned allocations{};IgbTypeRegistrationCall request;request.kind=IgbTypeRegistrationServiceKind::layout_probe;request.function=0x8002ad90;
    auto op=make_native_igb_field_probe_factory(f.services)(request);
    check(op && !op->resume().completed,"Memory adapter reused the legacy object-only stack callback.");
    f.services->profile_local_storage=[&](IgbMetaFieldKind kind){++allocations;check(kind==IgbMetaFieldKind::memory_reference,"Memory adapter selected another local profile.");return f.local;};
    f.services->runtime->singleton.reset();check(!op->resume().completed && allocations==1,"Memory adapter did not retain its selected stack object.");
    f.services->runtime->singleton=f.core;check(op->resume().completed && allocations==1,"Memory adapter repeated stack allocation after suspension.");
    Fixture out;out.memory();out.core->vtable_offset=0x50;IgbFieldProbeInvocation oversized(out.local,IgbMetaFieldKind::memory_reference);
    rejects([&]{oversized.resume(out.services);},"Memory probe accepted a word beyond its 0x50-byte stack object.");
}
void boolean_probe(){
    for(const auto offset:{0u,4u,0x24u,0x30u}){Fixture f;
        f.local.storage->bytes->bytes.resize(0x34);f.local.storage->bytes->initialized.resize(0x34);f.core->vtable_offset=offset;
        IgbFieldProbeInvocation run(f.local,IgbMetaFieldKind::boolean);
        check(run.resume(f.services).completed && run.value()==(offset==0?0x80473e30u:igb_bootstrap_read_word(f.local,offset)),
            "Boolean probe used the integer virtual identity or another local size.");
        check(f.field->vtable_word==0x80471914 && f.calls.empty(),"Boolean probe executed reference-field member cleanup.");
    }
    Fixture f;f.services->runtime->singleton.reset();IgbFieldProbeInvocation run(f.local,IgbMetaFieldKind::boolean);
    check(!run.resume(f.services).completed && f.field->vtable_word==0x80473e30,"Boolean probe did not execute reset8003EBC8.");
    auto name=string(1);igb_meta_field_set_name(f.local,name,0x1008);f.services->runtime->singleton=f.core;bool ready{};unsigned releases{};
    f.action=[&](const auto& c,auto& status){++releases;check(c.kind==K::release_string && c.string==name && name->references==0 &&
        f.field->vtable_word==0x80471914,"Boolean probe changed name release or base vtable order.");status.completed=ready;};
    check(!run.resume(f.services).completed && releases==1,"Boolean name release did not suspend.");ready=true;
    check(run.resume(f.services).completed && run.value()==0x80473e30 && releases==2 && name->references==0,
        "Boolean resume repeated reset, word capture, or name decrement.");
    Fixture adapter;unsigned allocations{};IgbTypeRegistrationCall request;request.kind=IgbTypeRegistrationServiceKind::layout_probe;request.function=0x80038778;
    auto op=make_native_igb_field_probe_factory(adapter.services)(request);
    check(op && !op->resume().completed,"Boolean adapter guessed a legacy object stack allocation.");
    adapter.services->profile_local_storage=[&](IgbMetaFieldKind kind){++allocations;check(kind==IgbMetaFieldKind::boolean,"Boolean adapter changed the selected profile.");return adapter.local;};
    check(op->resume().completed && allocations==1,"Boolean adapter failed late stack binding.");
    Fixture invalid;invalid.core->vtable_offset=0x34;IgbFieldProbeInvocation outside(invalid.local,IgbMetaFieldKind::boolean);
    rejects([&]{outside.resume(invalid.services);},"Boolean probe accepted an offset beyond its 0x34-byte local.");
}
void small_profile_probes(){
    for(const auto kind:{IgbMetaFieldKind::string,IgbMetaFieldKind::signed_byte,IgbMetaFieldKind::enumeration,IgbMetaFieldKind::pointer,IgbMetaFieldKind::unsigned_byte,IgbMetaFieldKind::unsigned_int}){
        const auto profile=igb_meta_field_profile(kind);
        for(std::uint32_t offset=0;offset<profile.bytes;offset+=4){Fixture f;
            f.local.storage->bytes->bytes.resize(profile.bytes);f.local.storage->bytes->initialized.resize(profile.bytes);f.core->vtable_offset=offset;
            IgbFieldProbeInvocation run(f.local,kind);
            check(run.resume(f.services).completed && run.value()==(offset==0?profile.vtable:igb_bootstrap_read_word(f.local,offset)),
                "Small-field probe did not use its exact local size and reset.");
            check(f.field->vtable_word==0x80471914 && f.calls.empty(),"Small-field probe executed a cache cleanup.");
            if(kind==IgbMetaFieldKind::string)check(f.field->flag_35==1 && f.field->flag_38==1 && f.field->flag_27==2 &&
                igb_bootstrap_read_word(f.local,0x38)==0x01cccccc,"String reset lost reference flags or changed padding.");
        }
        Fixture f;f.services->runtime->singleton.reset();IgbFieldProbeInvocation run(f.local,kind);
        check(!run.resume(f.services).completed && f.field->vtable_word==(kind==IgbMetaFieldKind::unsigned_byte?0x80471914u:profile.vtable),
            "New probe failed to suspend after the exact reset.");
        auto name=string(1);igb_meta_field_set_name(f.local,name,0x1008);f.services->runtime->singleton=f.core;unsigned polls{};bool ready{};
        f.action=[&](const IgbDependenciesCall& c,MessengerCallResult& status){++polls;
            check(c.kind==K::release_string && c.string==name && name->references==0 &&
                f.field->vtable_word==(polls==1?0x80471914u:0x12345678u),
                "New probe changed base cleanup or full-word name release.");status.completed=ready;};
        check(!run.resume(f.services).completed && polls==1,"New probe did not retain its pending name release.");
        f.core->vtable_offset=4;igb_bootstrap_write_word(f.local,0,0x12345678);ready=true;
        check(run.resume(f.services).completed && run.value()==profile.vtable && polls==2 && name->references==0 && f.field->vtable_word==0x12345678 &&
            igb_bootstrap_read_word(f.local,0x0c)==0x1008,"Pending probe replayed capture, destructor stores or reference decrement.");
        for(const auto references:{0u,0x800001u}){Fixture wrapping;wrapping.services->runtime->singleton.reset();IgbFieldProbeInvocation wrap(wrapping.local,kind);
            check(!wrap.resume(wrapping.services).completed,"Name wrap fixture did not pause after reset.");
            auto header=string(references);igb_meta_field_set_name(wrapping.local,header,0x2008);wrapping.services->runtime->singleton=wrapping.core;
            check(wrap.resume(wrapping.services).completed && header->references==references-1u && wrapping.calls.empty(),
                "New probe used packed object counts for string cleanup.");
        }
        Fixture late;late.services->runtime->singleton.reset();IgbFieldProbeInvocation capture(late.local,kind);
        check(!capture.resume(late.services).completed,"Late string binding fixture did not pause.");
        igb_bootstrap_write_word(late.local,0x0c,0x3008);late.services->runtime->singleton=late.core;
        check(!capture.resume(late.services).completed,"Missing name binding was fabricated.");
        auto captured=string(2),replacement=string(7);late.services->strings[0x3008]=captured;igb_meta_field_set_name(late.local,replacement,0x4008);
        check(capture.resume(late.services).completed && captured->references==1 && replacement->references==7,
            "Late name binding redirected the captured original pointer.");
        Fixture zero;zero.local.storage->original_address=0xfffffff4;zero.services->runtime->singleton.reset();IgbFieldProbeInvocation skip(zero.local,kind);
        check(!skip.resume(zero.services).completed,"Member-address guard fixture did not pause.");auto ignored=string(1);
        igb_meta_field_set_name(zero.local,ignored,0x5008);zero.services->runtime->singleton=zero.core;
        check(skip.resume(zero.services).completed && ignored->references==1 && zero.calls.empty(),"Probe ignored the original zero member-address branch.");
        Fixture adapter;IgbTypeRegistrationCall request;request.kind=IgbTypeRegistrationServiceKind::layout_probe;
        request.function=kind==IgbMetaFieldKind::string?0x80025348:kind==IgbMetaFieldKind::signed_byte?0x80037c78:
            kind==IgbMetaFieldKind::enumeration?0x800314d4:kind==IgbMetaFieldKind::unsigned_byte?0x80023180:
            kind==IgbMetaFieldKind::unsigned_int?0x80022c28:0x80027648;
        auto operation=make_native_igb_field_probe_factory(adapter.services)(request);
        check(operation && !operation->resume().completed,"New probe guessed the legacy object local.");unsigned allocations{};
        adapter.services->profile_local_storage=[&](IgbMetaFieldKind selected){++allocations;check(selected==kind,"Probe adapter selected another profile.");return adapter.local;};
        check(operation->resume().completed && allocations==1,"New probe adapter did not resume its late local binding.");
        Fixture outside;outside.core->vtable_offset=profile.bytes;IgbFieldProbeInvocation invalid(outside.local,kind);
        rejects([&]{invalid.resume(outside.services);},"New probe accepted a word beyond its original local object.");
    }
    Fixture padding;padding.core->vtable_offset=0x38;padding.local.storage->bytes->initialized[0x39]=0;
    IgbFieldProbeInvocation unreadable(padding.local,IgbMetaFieldKind::string);
    rejects([&]{unreadable.resume(padding.services);},"String reset fabricated initialized trailing padding.");
    check(padding.field->vtable_word==0x8047693c,"String padding fault performed later cleanup.");
}
void enum_function_and_pointer_padding(){
    Fixture enumeration;enumeration.local.storage->bytes->bytes.resize(0x38);enumeration.local.storage->bytes->initialized.resize(0x38);
    igb_bootstrap_write_word(enumeration.local,0x34,0x12345678);enumeration.field->method_function=0x12345678;enumeration.core->vtable_offset=0x34;
    IgbFieldProbeInvocation reset(enumeration.local,IgbMetaFieldKind::enumeration);
    check(reset.resume(enumeration.services).completed && reset.value()==0x12345678 && enumeration.field->method_function==0x12345678 &&
        igb_bootstrap_read_word(enumeration.local,0x34)==0x12345678,"Enum probe initialized or released the untouched derived function pointer.");
    for(const auto kind:{IgbMetaFieldKind::enumeration,IgbMetaFieldKind::pointer}){
        Fixture unknown;unknown.local.storage->bytes->bytes.resize(0x38);unknown.local.storage->bytes->initialized.resize(0x38);
        unknown.core->vtable_offset=0x34;unknown.local.storage->bytes->initialized[kind==IgbMetaFieldKind::enumeration?0x34:0x36]=0;
        IgbFieldProbeInvocation invalid(unknown.local,kind);
        rejects([&]{invalid.resume(unknown.services);},"Small-field reset fabricated initialized derived padding or an enum getter.");
        check(invalid.faulted() && unknown.field->vtable_word==igb_meta_field_profile(kind).vtable && unknown.calls.empty(),
            "Derived read fault ran later base or name cleanup.");
    }
}
void unsigned_int_capture_order(){
    Fixture missing;missing.services->runtime->singleton.reset();
    IgbFieldProbeInvocation first(missing.local,IgbMetaFieldKind::unsigned_int);
    check(!first.resume(missing.services).completed && missing.field->vtable_word==0x80476e6c &&
        igb_bootstrap_read_word(missing.local,0)==0x80476e6c && missing.field->reference_word==1,
        "Unsigned integer specialized reset did not precede the core capture.");
    missing.services->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
    rejects([&]{first.resume(missing.services);},"Unsigned integer accepted a represented null core.");
    check(first.faulted() && missing.field->vtable_word==0x80476e6c && missing.calls.empty(),
        "Null unsigned integer core ran the later base cleanup.");

    Fixture captured;captured.core->vtable_offset.reset();
    IgbFieldProbeInvocation wait(captured.local,IgbMetaFieldKind::unsigned_int);
    check(!wait.resume(captured.services).completed && captured.field->vtable_word==0x80476e6c,
        "Unknown offset skipped the unsigned integer reset prefix.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=4;
    captured.services->runtime->singleton=replacement;captured.core->vtable_offset=0;
    igb_bootstrap_write_word(captured.local,0,0x12345678);
    check(wait.resume(captured.services).completed && wait.value()==0x12345678 && captured.field->vtable_word==0x80471914,
        "Unsigned integer probe reloaded the core or repeated reset before its selected-word read.");

    Fixture callback;callback.services->fields->dependencies->globals->string_pool.reset();
    auto fresh=std::make_shared<IgbRuntimeTypeRegistry>();fresh->vtable_offset=4;
    callback.action=[&](const IgbDependenciesCall& c,MessengerCallResult&){
        if(c.kind==K::initialize_string_pool)callback.services->runtime->singleton=fresh;
    };
    IgbFieldProbeInvocation late(callback.local,IgbMetaFieldKind::unsigned_int);
    check(late.resume(callback.services).completed && late.value()==1 && callback.calls.size()==2,
        "Unsigned integer probe captured the core before reset dependency callbacks.");
    Fixture padding;padding.local.storage->bytes->bytes.resize(0x34);padding.local.storage->bytes->initialized.resize(0x34);
    padding.local.storage->bytes->initialized[0x16]=0;padding.core->vtable_offset=0x14;
    IgbFieldProbeInvocation invalid(padding.local,IgbMetaFieldKind::unsigned_int);
    rejects([&]{invalid.resume(padding.services);},"Unsigned integer fabricated initialized padding.");
    check(invalid.faulted() && padding.field->vtable_word==0x80476e6c && padding.local.storage->bytes->initialized[0x16]==0,
        "Unreadable unsigned integer word changed padding or performed base cleanup.");
}
void unsigned_byte_capture_order(){
    Fixture missing;missing.services->runtime->singleton.reset();
    IgbFieldProbeInvocation first(missing.local,IgbMetaFieldKind::unsigned_byte);
    check(!first.resume(missing.services).completed && missing.field->vtable_word==0x80471914 &&
        igb_bootstrap_read_word(missing.local,0)==0x80471914 && missing.field->reference_word==1,
        "Unsigned-byte probe published its derived table before the original core-pointer load.");
    missing.services->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
    rejects([&]{first.resume(missing.services);},"Unsigned-byte probe accepted a known-null captured core.");
    check(first.faulted() && missing.field->vtable_word==0x80470a44 && missing.calls.empty(),
        "Null core fault skipped the preceding unsigned-byte table store or ran cleanup.");

    Fixture captured;captured.core->vtable_offset.reset();
    IgbFieldProbeInvocation wait(captured.local,IgbMetaFieldKind::unsigned_byte);
    check(!wait.resume(captured.services).completed && captured.field->vtable_word==0x80470a44,
        "Unknown offset did not retain the unsigned-byte derived-table publication.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=4;
    captured.services->runtime->singleton=replacement;captured.core->vtable_offset=0;
    igb_bootstrap_write_word(captured.local,0,0x12345678);
    check(wait.resume(captured.services).completed && wait.value()==0x12345678 && captured.field->vtable_word==0x80471914,
        "Unsigned-byte resume reloaded the core, repeated derived publication, or changed the captured word.");

    Fixture callback;callback.services->fields->dependencies->globals->string_pool.reset();
    auto fresh=std::make_shared<IgbRuntimeTypeRegistry>();fresh->vtable_offset=4;
    callback.action=[&](const IgbDependenciesCall& c,MessengerCallResult&){
        if(c.kind==K::initialize_string_pool)callback.services->runtime->singleton=fresh;
    };
    IgbFieldProbeInvocation late(callback.local,IgbMetaFieldKind::unsigned_byte);
    check(late.resume(callback.services).completed && late.value()==1 && callback.calls.size()==2,
        "Unsigned-byte probe captured the core before base reset's dependency callbacks.");

    Fixture padding;padding.local.storage->bytes->bytes.resize(0x34);padding.local.storage->bytes->initialized.resize(0x34);
    padding.local.storage->bytes->initialized[0x16]=0;padding.core->vtable_offset=0x14;
    IgbFieldProbeInvocation invalid(padding.local,IgbMetaFieldKind::unsigned_byte);
    rejects([&]{invalid.resume(padding.services);},"Unsigned-byte probe fabricated initialized base padding.");
    check(invalid.faulted() && padding.field->vtable_word==0x80470a44 && padding.local.storage->bytes->initialized[0x16]==0,
        "Unsigned-byte unreadable-word failure changed padding or performed later cleanup.");
}
}
int main(){try{reset_and_capture();cleanup_order();missing_bindings_and_failures();adapters();signed_int_probe();
    memory_reset_and_capture();memory_cleanup_order();memory_packed_counts_and_bindings();memory_pending_destructor();memory_failures_and_adapter();boolean_probe();
    small_profile_probes();enum_function_and_pointer_padding();unsigned_byte_capture_order();unsigned_int_capture_order();
    std::cout<<"Field layout probe tests passed: "<<checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
