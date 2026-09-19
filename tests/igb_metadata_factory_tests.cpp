#include "dw4/igb_metadata_factory.hpp"

#include <iostream>

namespace {
using namespace dw4;using K=IgbMetadataFactoryServiceKind;using B=IgbBootstrapServiceKind;using S=IgbDependenciesServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<ModelSceneObject> owner(){return std::make_shared<ModelSceneData>();}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t size){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;storage->bytes=std::make_shared<IgbScratchBuffer>();
    storage->bytes->bytes.assign(size,0xcc);storage->bytes->initialized.assign(size,1);return {storage,0};
}
struct Fixture {
    std::shared_ptr<IgbMetadataFactoryServices> services=std::make_shared<IgbMetadataFactoryServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbRuntimeMetaType> metadata=std::make_shared<IgbRuntimeMetaType>(),base=std::make_shared<IgbRuntimeMetaType>();
    std::shared_ptr<IgbDecodeFieldRegistry> fields=std::make_shared<IgbDecodeFieldRegistry>();
    std::shared_ptr<ModelSceneObject> pool=owner(),string_pool=owner();
    IgbBootstrapPointer object=allocation(0x120000,0x64),list=allocation(0x180000,16);
    std::vector<IgbBootstrapCall> bootstrap_calls;std::vector<IgbMetadataFactoryCall> calls;std::vector<IgbDependenciesCall> string_calls;
    std::function<void(const IgbBootstrapCall&,IgbBootstrapOperation&,MessengerCallResult&)> on_bootstrap;
    std::function<void(const IgbMetadataFactoryCall&,IgbMetadataFactoryOperation&,MessengerCallResult&)> on_call;
    std::function<void(const IgbDependenciesCall&,IgbDependenciesOperation&,MessengerCallResult&)> on_string;
    Fixture(){
        object.storage->bindings.push_back({0,{},metadata,{}});list.storage->bindings.push_back({0,{},{},fields});
        services->bootstrap=std::make_shared<IgbBootstrapServices>();services->bootstrap->runtime=std::make_shared<IgbRuntimeTypesServices>();
        services->bootstrap->runtime->singleton=core;services->bootstrap->default_pool_handle=7;base->allocation_prefix=0;
        services->bootstrap->object_metadata=std::make_shared<IgbRegistrationMetaSlot>(base);
        services->metadata=metadata;services->dependencies=std::make_shared<IgbDependenciesServices>();
        services->dependencies->globals=std::make_shared<SystemStageServices>();services->dependencies->globals->string_pool=string_pool;
        services->bootstrap->factory=[this](const IgbBootstrapCall& c){auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[this,c,out=op.get()]{
            bootstrap_calls.push_back(c);MessengerCallResult r{true,0,{}};
            if(c.kind==B::pool_for_handle || c.kind==B::allocation_pool)out->pool_result=pool;
            if(c.kind==B::pool_allocate || c.kind==B::heap_allocate)out->allocation_result=object;
            if(c.kind==B::pool_index)r.value=0xabcdefa5;
            if(on_bootstrap)on_bootstrap(c,*out,r);return r;};return op;};
        services->factory=[this](const IgbMetadataFactoryCall& c){auto op=std::make_shared<IgbMetadataFactoryOperation>();op->resume=[this,c,out=op.get()]{
            calls.push_back(c);MessengerCallResult r{true,0,{}};
            if(c.kind==K::reflected_factory)out->allocation_result=object;
            if(c.kind==K::create_fields){out->allocation_result=list;out->fields_result=fields;}
            if(on_call)on_call(c,*out,r);return r;};return op;};
        services->dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[this,c,out=op.get()]{
            string_calls.push_back(c);MessengerCallResult r{true,0,{}};
            if(c.kind==S::create_string_pool || c.kind==S::initialize_string_pool)out->pool_result=string_pool;
            if(on_string)on_string(c,*out,r);return r;};return op;};
    }
};
void exact_direct_defaults(){
    Fixture f;f.core->enabled=1;IgbMetadataConstructorInvocation call(f.object);
    check(call.resume(f.services).completed && call.result()==f.metadata,"Direct metadata constructor did not return its typed owner.");
    check(igb_bootstrap_read_word(f.object,0)==0x804761d8 && f.metadata->reference_word==1,"Final metadata vtable/reference reset differs.");
    check(f.bootstrap_calls.size()==2 && f.bootstrap_calls[0].kind==B::allocation_pool && f.bootstrap_calls[1].kind==B::pool_index,"Enabled base constructor skipped allocation tag lookup.");
    check(igb_bootstrap_read_word(f.object,4)==1,"Second base reset did not erase the earlier allocation tag.");
    check(f.metadata->major==1 && f.metadata->minor==0 && f.metadata->flag_18==0 && f.metadata->flag_19==1 && f.metadata->flag_1a==0,"Version or metadata byte defaults differ.");
    check(f.metadata->native_size==0xffffffffu && f.metadata->native_alignment==4,"Metadata unknown-size/alignment defaults differ.");
    check(f.metadata->allocation_prefix==0 && f.metadata->factory_redirect==0 && f.metadata->instance_count==0,"Metadata scalar pointer/profile defaults are missing.");
    check(f.metadata->fields==f.fields && igb_bootstrap_read_word(f.object,0x28)==0x180000,"Constructed field list was not published at+28.");
    check(f.calls.size()==1 && f.calls.front().kind==K::create_fields && !f.calls.front().pool,"Field-list factory did not receive its original null pool.");
    check(f.string_calls.empty(),"Existing string pool triggered lazy allocation or a fake intern callback.");
    for(auto offset:{0x1bu,0x5au,0x5bu})check(igb_bootstrap_read_byte(f.object,offset)==0xcc,"Constructor overwrote physical padding.");
    const auto count=f.calls.size();check(call.resume().completed && f.calls.size()==count,"Completed constructor replayed list creation.");
    Fixture reset;IgbMetadataConstructorInvocation reset_only(reset.object,IgbMetadataConstructor::reset);
    check(reset_only.resume(reset.services).completed && !reset.metadata->fields && reset.calls.empty() && reset.bootstrap_calls.empty(),"Reset-only entry incorrectly called constructors or allocation tag selection.");
}
void factory_branches(){
    Fixture f;IgbMetadataFactoryInvocation call(f.pool);check(call.resume(f.services).completed && call.result()==f.metadata,"Disabled metadata factory failed.");
    check(f.bootstrap_calls.size()==1 && f.bootstrap_calls[0].kind==B::pool_allocate && f.bootstrap_calls[0].pool==f.pool && f.bootstrap_calls[0].bytes==0x64,"Factory changed captured allocation pool/size.");
    check(call.allocation().storage==f.object.storage,"Factory lost its physical allocation.");
    Fixture reflected;reflected.core->enabled=255;auto next=std::make_shared<IgbRuntimeMetaType>();const auto input_pool=reflected.pool;
    reflected.on_call=[&](const auto& c,auto&,auto&){if(c.kind==K::register_type){reflected.services->metadata=next;reflected.pool=owner();}
        if(c.kind==K::reflected_factory)check(c.metadata==next && c.pool==input_pool,"Reflected factory used stale metadata or changed captured pool.");};
    IgbMetadataFactoryInvocation active(input_pool);check(active.resume(reflected.services).completed,"Enabled factory did not complete its explicit reflected operation.");
    check(reflected.calls.size()==2 && reflected.bootstrap_calls.empty() && igb_bootstrap_read_word(reflected.object,0)==0xcccccccc,"Enabled path ran direct construction.");
    Fixture late;late.core->enabled=1;late.services->metadata.reset();IgbMetadataFactoryInvocation missing;
    check(!missing.resume(late.services).completed && late.calls.size()==1,"Missing metadata global did not pause after registration.");
    late.services->metadata=std::shared_ptr<IgbRuntimeMetaType>{};late.on_call=[](const auto& c,auto& op,auto&){if(c.kind==K::reflected_factory){check(!c.metadata,"Null metadata global was not preserved.");op.allocation_result={};}};
    check(missing.resume(late.services).completed && !missing.result() && late.calls.size()==2,"Null reflected return or registration continuation differs.");
    Fixture null;null.on_bootstrap=[](const auto& c,auto& op,auto&){if(c.kind==B::pool_allocate)op.allocation_result={};};
    IgbMetadataFactoryInvocation failed(null.pool);rejects([&]{failed.resume(null.services);},"Null direct allocation incorrectly returned success.");
    check(failed.faulted() && null.calls.empty(),"Null direct allocation reached a constructor.");
}
void lazy_strings_and_release(){
    Fixture f;f.services->dependencies->globals->string_pool.reset();auto old=std::make_shared<IgbInternedString>();old->value=std::make_shared<const std::string>("old");old->container=owner();
    bool ready{};f.on_string=[&](const auto& c,auto&,auto& r){
        if(c.kind==S::initialize_string_pool)igb_metadata_set_pool_name(f.object,old,0x170108);
        if(c.kind==S::release_string){check(c.string==old && c.pool==old->container && old->references==0,"String release lost the captured full-word decrement or container.");r.completed=ready;}
    };
    IgbMetadataConstructorInvocation call(f.object);
    check(!call.resume(f.services).completed && old->references==0 && igb_bootstrap_read_word(f.object,0x60)==0x170108,"Pending final string release lost its reached prefix.");
    check(f.metadata->fields==nullptr && f.calls.empty(),"Field list was created before the pending final release.");
    check(f.string_calls.size()==3 && f.string_calls[0].bytes==16 && f.string_calls[1].pool==f.string_pool,"Lazy string pool allocation/initialization ordering differs.");
    ready=true;check(call.resume(f.services).completed && old->references==0 && igb_bootstrap_read_word(f.object,0x60)==0,"Release continuation replayed decrement or omitted null publication.");
    check(f.string_calls.size()==4,"Release continuation replayed string pool allocation.");
    Fixture null;null.services->dependencies->globals->string_pool.reset();null.on_string=[](const auto& c,auto& op,auto&){if(c.kind==S::create_string_pool)op.pool_result.reset();};
    IgbMetadataConstructorInvocation null_pool(null.object,IgbMetadataConstructor::reset);
    check(null_pool.resume(null.services).completed && null.string_calls.size()==1 && !null.services->dependencies->globals->string_pool,"Null string-pool allocation incorrectly dereferenced the null-name leaf.");
    Fixture unknown;unknown.services->dependencies->globals->string_pool.reset();unknown.on_string=[&](const auto& c,auto&,auto&){if(c.kind==S::initialize_string_pool)igb_bootstrap_write_word(unknown.object,0x60,0x1234);};
    IgbMetadataConstructorInvocation needs_owner(unknown.object,IgbMetadataConstructor::reset);
    check(!needs_owner.resume(unknown.services).completed,"Unmapped live string pointer was silently discarded.");
    auto binding=std::make_shared<IgbInternedString>();binding->references=2;igb_metadata_set_pool_name(unknown.object,binding,0x1234);
    check(needs_owner.resume(unknown.services).completed && binding->references==1 && unknown.string_calls.size()==2,"Late string binding replayed earlier construction.");
    Fixture replacement;replacement.services->dependencies->globals->string_pool.reset();
    auto released=std::make_shared<IgbInternedString>();released->container=owner();std::weak_ptr<IgbInternedString> overwritten;
    replacement.on_string=[&](const auto& c,auto&,auto&){
        if(c.kind==S::initialize_string_pool)igb_metadata_set_pool_name(replacement.object,released,0x170108);
        if(c.kind==S::release_string){auto inserted=std::make_shared<IgbInternedString>();inserted->references=11;overwritten=inserted;
            igb_metadata_set_pool_name(replacement.object,inserted,0x170208);}
    };
    {IgbMetadataConstructorInvocation publish(replacement.object,IgbMetadataConstructor::reset);
        check(publish.resume(replacement.services).completed,"String replacement fixture did not complete.");}
    check(!overwritten.expired() && overwritten.lock()->references==11 && released->references==0,
        "Raw null publication destroyed or guest-released the callback's newly installed owner.");
    check(igb_bootstrap_read_word(replacement.object,0x60)==0 && !*replacement.metadata->default_pool_string,
        "Retaining overwritten ownership incorrectly retained the live+60 pointer.");
    Fixture residual;residual.services->dependencies->globals->string_pool.reset();std::weak_ptr<IgbInternedString> remaining;
    residual.on_string=[&](const auto& c,auto&,auto&){if(c.kind==S::initialize_string_pool){
        auto inserted=std::make_shared<IgbInternedString>();inserted->references=2;remaining=inserted;
        igb_metadata_set_pool_name(residual.object,inserted,0x170308);}};
    {IgbMetadataConstructorInvocation publish(residual.object,IgbMetadataConstructor::reset);
        check(publish.resume(residual.services).completed,"Residual string reference fixture did not complete.");}
    check(!remaining.expired() && remaining.lock()->references==1 && residual.string_calls.size()==2,
        "Raw publication destroyed a string with a remaining guest reference.");
}
void failure_and_live_virtuals(){
    Fixture short_object;short_object.object.storage->bytes->bytes.resize(0x30);short_object.object.storage->bytes->initialized.resize(0x30);
    IgbMetadataConstructorInvocation short_call(short_object.object,IgbMetadataConstructor::reset);
    rejects([&]{short_call.resume(short_object.services);},"Short physical metadata storage was accepted.");
    check(igb_bootstrap_read_word(short_object.object,0)==0x804761d8 && igb_bootstrap_read_word(short_object.object,4)==1 && igb_bootstrap_read_word(short_object.object,0x1c)==0,"Short-object failure did not preserve reached stores.");
    Fixture missing;missing.services->dependencies.reset();IgbMetadataConstructorInvocation pending(missing.object);
    check(!pending.resume(missing.services).completed && missing.metadata->native_size==0xffffffffu && missing.metadata->fields==nullptr,"Missing string global did not retain completed defaults.");
    Fixture live;live.on_call=[&](const auto& c,auto&,auto&){
        if(c.kind==K::create_fields)igb_bootstrap_write_word(live.object,0,0x12340000);
        if(c.kind==K::object_virtual && c.virtual_slot==0x2c){check(c.vtable_word==0x12340000,"First tail virtual used an old vtable.");igb_bootstrap_write_word(live.object,0,0x56780000);}
        if(c.kind==K::object_virtual && c.virtual_slot==0x24)check(c.vtable_word==0x56780000 && c.argument==0,"Second tail virtual did not reload vtable or set argument zero.");
    };
    IgbMetadataFactoryInvocation factory(live.pool);check(factory.resume(live.services).completed && live.calls.size()==3,"Live virtual replacement did not reach both explicit callbacks.");
    Fixture reentry;IgbMetadataConstructorInvocation recursive(reentry.object);
    reentry.on_call=[&](const auto&,auto&,auto&){rejects([&]{recursive.resume(reentry.services);},"Recursive constructor call was accepted.");};
    rejects([&]{recursive.resume(reentry.services);},"Caught recursive fault allowed outer constructor completion.");
    check(recursive.faulted() && igb_bootstrap_read_word(reentry.object,0x28)==0,"Recursive failure published a field-list result.");
}
void real_list_composition(){
    Fixture f;auto list_services=std::make_shared<IgbFieldRegistryServices>();list_services->bootstrap=f.services->bootstrap;
    auto backing=allocation(0x200000,16);f.services->field_registries=list_services;
    f.on_bootstrap=[&](const auto& c,auto& op,auto&){if((c.kind==B::pool_allocate || c.kind==B::heap_allocate) && c.bytes==16)op.allocation_result=f.list;};
    list_services->factory=[&](const IgbFieldRegistryCall& c){auto op=std::make_shared<IgbFieldRegistryOperation>();op->resume=[&,c,out=op.get()]{
        if(c.kind==IgbFieldRegistryServiceKind::object_pool)out->pool_result=f.pool;
        else if(c.kind==IgbFieldRegistryServiceKind::allocate_backing)out->backing_result={backing,{}};
        else throw std::runtime_error("Unexpected list endpoint");return MessengerCallResult{true,0,{}};};return op;};
    IgbMetadataFactoryInvocation call(f.pool);check(call.resume(f.services).completed && call.result()->fields==f.fields,"Metadata constructor did not compose the actual list factory.");
    check(f.calls.empty() && igb_bootstrap_read_word(f.list,0)==0x80475cb0 && f.fields->count==0 && f.fields->backing_address==0x200000,"Composed list construction used a prepared success endpoint.");
    auto adapter=make_native_igb_metadata_factory(f.services);IgbTypeRegistrationCall request;request.kind=IgbTypeRegistrationServiceKind::set_name;
    check(!adapter(request),"Metadata adapter claimed unrelated registration operations.");
}
void retained_lifetime_and_identity(){
    Fixture f;f.services->dependencies.reset();auto call=std::make_unique<IgbMetadataConstructorInvocation>(f.object);
    check(!call->resume(f.services).completed,"Missing string global did not retain the constructor.");
    igb_bootstrap_write_word(f.object,0x48,123);
    IgbMetadataConstructorInvocation overlapping(f.object);
    rejects([&]{overlapping.resume(f.services);},"Overlapping metadata constructor was accepted.");
    check(igb_bootstrap_read_word(f.object,0x48)==123 && !f.metadata->construction_fault_,"Rejected overlap modified or poisoned the active constructor.");
    call.reset();check(f.metadata->construction_fault_,"Abandoned constructor did not poison unfinished metadata.");
    IgbMetadataConstructorInvocation abandoned(f.object);rejects([&]{abandoned.resume(f.services);},"Abandoned construction could be replayed.");
    Fixture stable;IgbMetadataFactoryInvocation complete(stable.pool);check(complete.resume(stable.services).completed,"Stable identity fixture did not construct.");
    stable.object.storage->bindings[0].metadata=std::make_shared<IgbRuntimeMetaType>();
    check(complete.result()==stable.metadata,"Completed factory reread a changed allocation binding.");
    Fixture mismatch;mismatch.on_call=[](const auto& c,auto& op,auto&){if(c.kind==K::create_fields)op.allocation_result={};};
    IgbMetadataConstructorInvocation invalid(mismatch.object);rejects([&]{invalid.resume(mismatch.services);},"Inconsistent field result/address was published.");
    check(igb_bootstrap_read_word(mismatch.object,0x28)==0,"Invalid field binding changed the original publication prefix.");
}
}
int main(){try{exact_direct_defaults();factory_branches();lazy_strings_and_release();failure_and_live_virtuals();real_list_composition();retained_lifetime_and_identity();
    std::cout<<"Metadata factory tests passed: "<<checks<<" checks\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
