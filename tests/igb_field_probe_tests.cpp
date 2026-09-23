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
};
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
}
int main(){try{reset_and_capture();cleanup_order();missing_bindings_and_failures();adapters();
    std::cout<<"Field layout probe tests passed: "<<checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
