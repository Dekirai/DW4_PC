#include "dw4/igb_field_prototype.hpp"

#include <iostream>

// The already admitted signed-int prototype isolates this physical ownership
// prerequisite from the new signed-long profile and pool registration tests.
namespace {
using namespace dw4;
using K=IgbFieldPrototypeServiceKind;
using P=IgbPointerListServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& operation,const char* why){bool failed{};try{operation();}catch(const std::runtime_error&){failed=true;}check(failed,why);}
IgbBootstrapPointer raw(std::uint32_t address,std::size_t size){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(size,0xcc);
    storage->bytes->initialized.assign(size,0);return {storage,0};
}
struct Fixture {
    std::shared_ptr<IgbFieldPrototypeServices> services=std::make_shared<IgbFieldPrototypeServices>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<IgbDecodeRuntimeField> field=std::make_shared<IgbDecodeRuntimeField>();
    std::shared_ptr<IgbDecodeFieldRegistry> registry=std::make_shared<IgbDecodeFieldRegistry>();
    IgbBootstrapPointer physical=raw(0x200000,0x34),backing=raw(0x300000,16);
    unsigned reflected{},pointer_polls{},finalized{},destroyed{};
    bool append_ready{true};
    std::function<void(const IgbPointerListCall&)> pointer_action;
    std::function<MessengerCallResult(const IgbFieldPrototypeCall&)> destroy_action;
    std::function<void(const IgbFieldOperationCall&)> finalize_action;
    Fixture(){
        field->physical_storage=IgbFieldStorageBinding{physical.storage,0};
        physical.storage->bindings.push_back({0,{},{},{},{},field});
        igb_bootstrap_write_word(physical,0,0x80475b60);
        igb_bootstrap_write_word(physical,4,7);
        igb_bootstrap_write_word(physical,0x1c,0);
        field->reference_word=100; // Deliberately stale, without changing guest bytes.
        services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();
        auto registration=services->registration;
        registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
        auto metadata=std::make_shared<IgbRuntimeMetaType>();
        registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::signed_int)]=
            std::make_shared<IgbRegistrationMetaSlot>(metadata);
        registration->types=std::make_shared<IgbTypeRegistrationServices>();registration->types->metadata_pool_handle=5;
        registration->pools=std::make_shared<IgbFilePoolsServices>();
        registration->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        registration->pools->memory_pools->selector=0;registration->pools->memory_pools->fallback=manager;
        manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->values=std::make_shared<IgbFieldValuesServices>();
        services->values->signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->registry=std::make_shared<std::optional<std::shared_ptr<IgbDecodeFieldRegistry>>>(registry);
        registry->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(4);
        registry->backing_bytes=backing.storage->bytes;registry->backing_address=0x300000;
        services->schema=std::make_shared<IgbFieldSchemaServices>();
        services->schema->fields=std::make_shared<IgbFieldOperationsServices>();
        services->schema->fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        services->schema->fields->pointer_lists->factory=[this](const IgbPointerListCall& call){
            auto operation=std::make_shared<IgbPointerListOperation>();
            operation->resume=[this,call,out=operation.get()]{
                ++pointer_polls;if(pointer_action)pointer_action(call);
                if(call.kind==P::allocation_pool || call.kind==P::object_pool)out->pool_result=pool;
                else if(call.kind==P::allocation_size){
                    if(!append_ready)return MessengerCallResult{false,0,"Prepared existing backing size."};
                    out->size_result=16;
                }else throw std::runtime_error("Unprepared prototype registry growth.");
                return MessengerCallResult{true,0,{}};
            };return operation;
        };
        // Named prepared finalizer virtuals isolate physical prototype ownership.
        services->schema->fields->factory=[this](const IgbFieldOperationCall& call){
            check(call.kind==IgbFieldOperationKind::field_virtual,"Unexpected prototype finalizer service.");
            auto operation=std::make_shared<IgbFieldOperation>();operation->resume=[this,call]{
                ++finalized;if(finalize_action)finalize_action(call);return MessengerCallResult{true,call.virtual_slot==0x64?4u:0u,{}};
            };return operation;
        };
        services->factory=[this](const IgbFieldPrototypeCall& call){
            auto operation=std::make_shared<IgbFieldPrototypeOperation>();
            operation->resume=[this,call,out=operation.get()]{
                if(call.kind==K::reflected_field){check(call.function==0x8006546c && call.pool==pool,"Prototype changed the prepared reflection contract.");
                    ++reflected;out->field_result=field;out->field_address=0x200000;
                }else{check(call.kind==K::destroy_field,"Unexpected prototype operation.");++destroyed;if(destroy_action)return destroy_action(call);}
                return MessengerCallResult{true,0,{}};
            };return operation;
        };
    }
};

void physical_retain_precedes_pending_append(){
    Fixture f;f.append_ready=false;IgbFieldPrototypeInvocation invocation(IgbFieldTypeKind::signed_int);
    check(!invocation.resume(f.services).completed && f.services->values->signed_int_prototype==f.field &&
        igb_bootstrap_read_word(f.physical,4)==8 && f.field->reference_word==8 && f.registry->count==0 && f.reflected==1,
        "Prototype did not publish, physically retain, then suspend before insertion.");
    check(!invocation.resume(f.services).completed && igb_bootstrap_read_word(f.physical,4)==8,
        "Suspended append replayed the physical retain.");
    f.append_ready=true;f.services->factory={};
    check(invocation.resume(f.services).completed && f.registry->count==1 && f.registry->backing->at(0)==f.field &&
        igb_bootstrap_read_word(f.physical,4)==7 && f.field->reference_word==7 && f.destroyed==0,
        "Creator release used a stale cache or changed the append prefix.");
}

void reflection_identity_is_captured_before_registry(){
    for(const bool replace_storage:{false,true}){
        Fixture f;f.services->registry->reset();
        IgbFieldPrototypeInvocation invocation(IgbFieldTypeKind::signed_int);
        check(!invocation.resume(f.services).completed && f.reflected==1 && f.pointer_polls==0 &&
            igb_bootstrap_read_word(f.physical,4)==7,"Unknown registry performed an early prototype reference access.");
        const auto original=f.physical;auto replacement=raw(0x210000,0x34);
        replacement.storage->bindings.push_back({0,{},{},{},{},f.field});
        if(replace_storage)f.field->physical_storage=IgbFieldStorageBinding{replacement.storage,0};
        else{replacement.storage->original_address=original.storage->original_address;
            original.storage->bytes=replacement.storage->bytes;replacement=original;}
        igb_bootstrap_write_word(replacement,4,41);
        const auto replacement_bytes=replacement.storage->bytes->bytes;
        const auto replacement_mask=replacement.storage->bytes->initialized;
        *f.services->registry=f.registry;
        rejects([&]{invocation.resume(f.services);},"Registry continuation silently redirected the captured reflected field reference.");
        check(invocation.faulted() && f.pointer_polls==0 && f.registry->count==0 &&
            replacement.storage->bytes->bytes==replacement_bytes && replacement.storage->bytes->initialized==replacement_mask,
            "Invalid captured reflection identity changed replacement bytes or began append.");
        f.field->physical_storage=IgbFieldStorageBinding{original.storage,0};
        rejects([&]{invocation.resume(f.services);},"Repair revived a faulted reflected-reference continuation.");
    }
}

void header_fault_occurs_after_registry_lookup(){
    for(const bool truncate:{false,true}){
        Fixture f;if(truncate){f.physical.storage->bytes->bytes.resize(7);f.physical.storage->bytes->initialized.resize(7);}
        else for(unsigned i=4;i<8;++i)f.physical.storage->bytes->initialized[i]=0;
        f.services->registry->reset();IgbFieldPrototypeInvocation invocation(IgbFieldTypeKind::signed_int);
        check(!invocation.resume(f.services).completed && !invocation.faulted() && f.reflected==1 &&
            f.services->values->signed_int_prototype==f.field,"Capturing reflection eagerly validated its unreached header.");
        *f.services->registry=f.registry;
        rejects([&]{invocation.resume(f.services);},"Reached unavailable physical prototype reference used its canonical cache.");
        check(f.pointer_polls==0 && f.registry->count==0 && f.field->reference_word==100,
            "Failed retain modified the canonical count or registry prefix.");
    }
}

void unfinished_capture_keeps_original_bytes(){
    Fixture f;f.services->registry->reset();std::weak_ptr<IgbScratchBuffer> captured=f.physical.storage->bytes;
    {
        IgbFieldPrototypeInvocation invocation(IgbFieldTypeKind::signed_int);
        check(!invocation.resume(f.services).completed,"Missing registry did not suspend the reflected prototype.");
        f.physical.storage->bytes=raw(0x210000,0x34).storage->bytes;
    }
    check(!captured.expired(),"Abandoned reflected prototype dropped the original captured reference bytes.");
    f.services->retained.clear();
    check(captured.expired(),"Captured reference retention introduced an ownership cycle.");
}

auto bound_field(std::uint32_t address,std::uint32_t references){
    auto field=std::make_shared<IgbDecodeRuntimeField>();auto physical=raw(address,0x34);
    physical.storage->bindings.push_back({0,{},{},{},{},field});field->physical_storage=IgbFieldStorageBinding{physical.storage,0};
    igb_bootstrap_write_word(physical,0,0x80475b60);igb_bootstrap_write_word(physical,4,references);
    igb_bootstrap_write_word(physical,0x1c,0);return std::pair{field,physical};
}

void fresh_creator_then_fresh_registry_ordinal_and_finalizer(){
    Fixture f;f.append_ready=false;auto [released,released_bytes]=bound_field(0x210000,0x80000001);
    auto [ordinal,ordinal_bytes]=bound_field(0x220000,5);auto [unused,unused_bytes]=bound_field(0x230000,6);
    released->reference_word=99;unused->ordinal=0x55;bool destroy_ready{};unsigned destroy_selections{};
    f.pointer_action=[&](const IgbPointerListCall& call){
        if(call.kind==P::allocation_size && f.append_ready)f.services->values->signed_int_prototype=released;
    };
    f.destroy_action=[&](const IgbFieldPrototypeCall& call){
        ++destroy_selections;check(call.field==released && call.function==0x80066e1c &&
            igb_bootstrap_read_word(released_bytes,4)==0x80000000 && f.registry->count==1 &&
            f.registry->backing->at(0)==f.field,"Creator release lost its fresh owner or physical packed-count prefix.");
        if(!destroy_ready)return MessengerCallResult{false,0,"Prepared creator destruction."};
        released_bytes.storage->bytes->released=true;f.services->values->signed_int_prototype=ordinal;f.services->registry->reset();
        return MessengerCallResult{true,0,{}};
    };
    IgbFieldPrototypeInvocation invocation(IgbFieldTypeKind::signed_int);
    check(!invocation.resume(f.services).completed && igb_bootstrap_read_word(f.physical,4)==8,"Initial prototype retain did not precede append.");
    f.append_ready=true;
    check(!invocation.resume(f.services).completed && destroy_selections==1 && f.destroyed==1 && released->reference_word==0x80000000 &&
        igb_bootstrap_read_word(f.physical,4)==8,"Creator release acted on the captured returned field or stale canonical count.");
    destroy_ready=true;check(!invocation.resume(f.services).completed && f.destroyed==2 && f.finalized==0,
        "Pending destruction repeated the decrement or skipped the second fresh registry getter.");
    auto fresh_registry=std::make_shared<IgbDecodeFieldRegistry>();fresh_registry->count=65538;
    *f.services->registry=fresh_registry;
    f.finalize_action=[&](const IgbFieldOperationCall& call){
        check(call.field==ordinal,"Finalizer recaptured a later prototype global.");
        f.services->values->signed_int_prototype=unused;
    };
    check(invocation.resume(f.services).completed && ordinal->ordinal==1 &&
        igb_bootstrap_read_byte(ordinal_bytes,0x12)==0 && igb_bootstrap_read_byte(ordinal_bytes,0x13)==1 &&
        unused->ordinal==0x55 && f.destroyed==2 && f.registry->count==1,
        "Prototype failed the second count, low16 ordinal, or captured finalizer ordering.");
}

void full_word_wrap_and_post_append_unknown_creator(){
    Fixture wrap;igb_bootstrap_write_word(wrap.physical,4,0xffffffff);wrap.field->reference_word=12;
    wrap.append_ready=false;IgbFieldPrototypeInvocation invocation(IgbFieldTypeKind::signed_int);
    check(!invocation.resume(wrap.services).completed && igb_bootstrap_read_word(wrap.physical,4)==0,
        "Prototype retain did not wrap the full physical word.");
    wrap.append_ready=true;check(invocation.resume(wrap.services).completed && igb_bootstrap_read_word(wrap.physical,4)==0xffffffff &&
        wrap.destroyed==0,"Creator decrement changed packed high bits or used stale canonical input.");

    Fixture unknown;unknown.pointer_action=[&](const IgbPointerListCall& call){
        if(call.kind==P::allocation_size)unknown.services->values->signed_int_prototype.reset();};
    IgbFieldPrototypeInvocation waiting(IgbFieldTypeKind::signed_int);
    check(!waiting.resume(unknown.services).completed && unknown.registry->count==1 &&
        igb_bootstrap_read_word(unknown.physical,4)==8 && !waiting.faulted(),"Unknown fresh creator global undid or skipped the completed insertion.");
    unknown.services->values->signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    rejects([&]{waiting.resume(unknown.services);},"Known-null fresh creator was treated as an unknown cell.");
    check(waiting.faulted() && unknown.registry->count==1 && igb_bootstrap_read_word(unknown.physical,4)==8,
        "Null creator fault changed the retained insertion prefix.");
}
} // namespace

int main(){try{physical_retain_precedes_pending_append();reflection_identity_is_captured_before_registry();
    header_fault_occurs_after_registry_lookup();unfinished_capture_keeps_original_bytes();
    fresh_creator_then_fresh_registry_ordinal_and_finalizer();full_word_wrap_and_post_append_unknown_creator();
    std::cout<<"Prototype physical reference checks: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"Prototype physical reference failure after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
