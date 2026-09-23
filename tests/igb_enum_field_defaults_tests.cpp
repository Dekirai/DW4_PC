#include "dw4/igb_enum_field_defaults.hpp"
#include "dw4/igb_field_values.hpp"
#include "dw4/igb_stream.hpp"

#include <bit>
#include <iostream>

namespace {
using namespace dw4;
using SK=IgbFieldSchemaServiceKind;using FK=IgbFieldOperationKind;using PK=IgbPointerListServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& action,const char* message){++checks;try{action();}catch(const std::runtime_error&){return;}throw std::runtime_error(message);}
auto bytes(std::size_t count,std::uint8_t value=0){auto b=std::make_shared<IgbScratchBuffer>();b->bytes.assign(count,value);b->initialized.assign(count,1);return b;}
auto metadata(){auto m=std::make_shared<IgbRuntimeMetaType>();m->fields=std::make_shared<IgbDecodeFieldRegistry>();
    m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(8);m->fields->backing_bytes=bytes(32);m->fields->backing_address=0x900000;return m;}
auto field(){auto f=std::make_shared<IgbDecodeRuntimeField>();f->vtable_word=0x80476630;f->name_string=std::shared_ptr<IgbInternedString>{};
    f->default_bytes=IgbRuntimeFieldDefaultSource{};f->default_bytes_address=0;f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
    f->flag_26=0xa6;f->flag_27=0xa7;f->flag_34=0xb4;f->method_function=0x12345678;return f;}
IgbBootstrapPointer physical(const std::shared_ptr<IgbDecodeRuntimeField>& f,std::uint32_t address=0x100000){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-8;storage->bytes=bytes(0x48,0xcc);
    storage->bindings.push_back({8,{},{},{},{},f});f->physical_storage=IgbFieldStorageBinding{storage,8};
    IgbBootstrapPointer p{storage,8};igb_bootstrap_write_word(p,0,*f->vtable_word);igb_bootstrap_write_word(p,4,f->reference_word);
    igb_bootstrap_write_word(p,0x20,0);igb_bootstrap_write_byte(p,0x26,f->flag_26);igb_bootstrap_write_word(p,0x34,0x12345678);return p;
}
struct Fixture {
    std::shared_ptr<IgbEnumFieldDefaultsServices> services=std::make_shared<IgbEnumFieldDefaultsServices>();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata();
    std::shared_ptr<IgbDecodeRuntimeField> local;
    std::shared_ptr<IgbRegistrationMetaSlot> cell=std::make_shared<IgbRegistrationMetaSlot>(target);
    std::shared_ptr<IgbFieldValuesServices> values=std::make_shared<IgbFieldValuesServices>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::vector<IgbBootstrapPointer> allocations;
    std::vector<std::uint32_t> selected_functions;
    std::function<void()> created_action,destroyed_action;
    bool create_ready=true,destroy_ready=true,with_physical=true;
    unsigned create_polls{},destroy_polls{},interns{},buffer_allocations{},field_calls{};
    Fixture(){
        services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();
        services->registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
        services->registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::enumeration)]=cell;
        services->schema=std::make_shared<IgbFieldSchemaServices>();auto& s=*services->schema;
        s.types=std::make_shared<IgbTypeRegistrationServices>();s.types->metadata_pool_handle=5;
        s.pools=std::make_shared<IgbFilePoolsServices>();s.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        s.pools->memory_pools->selector=0;s.pools->memory_pools->fallback=manager;
        manager->original_pool_access_bound=true;manager->pools[5]=pool;
        s.dependencies=std::make_shared<IgbDependenciesServices>();s.dependencies->globals=std::make_shared<SystemStageServices>();
        s.dependencies->globals->string_pool=pool;
        s.dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();
            op->resume=[this,c,out=op.get()]{
                if(c.kind==IgbDependenciesServiceKind::intern_name){++interns;auto name=std::make_shared<IgbInternedString>();
                    name->value=igb_dependency_name(c.name);name->container=pool;out->string_result=name;}
                else check(c.kind==IgbDependenciesServiceKind::release_string,"Unexpected prepared string endpoint.");
                return MessengerCallResult{true,0,{}};};return op;};
        s.fields=std::make_shared<IgbFieldOperationsServices>();s.fields->dependencies=s.dependencies;
        s.fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        s.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();
            op->resume=[this,c,out=op.get()]{
                if(c.kind==PK::allocation_pool)out->pool_result=pool;
                else{check(c.kind==PK::allocation_size,"Unexpected pointer-list growth endpoint.");out->size_result=32;}
                return MessengerCallResult{true,0,{}};};return op;};
        values->fields=s.fields;values->pointer_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        s.fields->factory=make_native_igb_field_values_factory(values,[this](const IgbFieldOperationCall& c){
            auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,out=op.get()]{++field_calls;std::uint32_t result{};
                if(c.kind==FK::object_pool || c.kind==FK::allocation_pool)out->pool_result=pool;
                else if(c.kind==FK::allocation_size)result=4;
                else{check(c.kind==FK::allocate_default && c.bytes==4,"Unexpected native default-buffer endpoint.");
                    ++buffer_allocations;out->buffer_result={bytes(c.bytes,0x5a),0};out->buffer_address=0x700000+buffer_allocations*0x100;}
                return MessengerCallResult{true,result,{}};};return op;});
        // The constructor and platform leaves are prepared. The native batch,
        // pointer append, schema, value setter and finalization execute fully.
        s.factory=[this](const IgbFieldSchemaCall& c){auto op=std::make_shared<IgbFieldSchemaOperation>();
            if(c.kind==SK::create_field){selected_functions.push_back(c.function);check(c.pool==pool,"Pointer constructor lost the selected pool.");}
            op->resume=[this,c,out=op.get(),done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};
                if(c.kind==SK::create_field){++create_polls;if(!create_ready)return MessengerCallResult{false,0,"prepared pointer constructor"};
                    local=field();out->field_result=local;out->field_address=0x100000;
                    if(with_physical)allocations.push_back(physical(local));if(created_action)created_action();}
                else{check(c.kind==SK::destroy_field && c.function==0x80066e1c,"Unexpected batch destruction.");
                    ++destroy_polls;if(!destroy_ready)return MessengerCallResult{false,0,"prepared descriptor destruction"};
                    if(destroyed_action)destroyed_action();}
                done=true;return MessengerCallResult{true,0,{}};};return op;};
    }
};
void complete_schema(){
    for(const bool mapped:{false,true}){Fixture f;f.with_physical=mapped;auto inherited=field();
        f.target->fields->count=1;f.target->fields->backing->at(0)=inherited;IgbEnumFieldDefaultsInvocation call;
        check(call.resume(f.services).completed && !call.faulted(),"Enum defaults did not complete its real one-field schema.");
        check(f.selected_functions==std::vector<std::uint32_t>{0x800274ec} && f.create_polls==1 && f.interns==1 && f.buffer_allocations==1,
            "Enum defaults changed its constructor or configured the wrong number of fields.");
        check(f.target->fields->count==2 && f.target->fields->backing->at(1)==f.local && f.local->reference_word==1 && inherited->flag_26==0xa6,
            "Enum defaults lost the captured first index or native creator release.");
        check(f.local->flag_26==0 && f.local->flag_27==0xa7 && f.local->flag_34==0xb4 && f.local->method_function==0x12345678,
            "Enum defaults changed an unrelated byte or manufactured a derived function value.");
        check(f.local->name && *f.local->name=="_getMetaEnumFunction" && f.local->offset==0x34 && f.local->declaring_type->lock()==f.target &&
            f.local->global_slot_address==0x80561c38 && *f.services->globals->at(0)->slot==f.local && f.local->type_ordinal==0xffff && f.local->size==4,
            "Enum defaults omitted native configuration or finalization.");
        check(f.local->default_bytes->buffer->bytes==std::vector<std::uint8_t>(4,0x5a),"Null-source finalization invented a scalar default.");
        if(mapped){const auto p=f.allocations[0];check(igb_bootstrap_read_byte(p,0x26)==0 && igb_bootstrap_read_byte(p,0x27)==0xcc &&
                igb_bootstrap_read_word(p,0x34)==0x12345678 && p.storage->bytes->bytes.front()==0xcc,"Physical flag store changed neighboring or derived bytes.");}
        check(call.resume().completed && f.create_polls==1 && f.interns==1,"Completed enum defaults replayed work.");
    }
}
void captured_receiver_and_live_registry(){
    Fixture f;auto other=metadata();f.target->fields->count=2;auto old_fields=f.target->fields;
    auto replacement=metadata()->fields;replacement->count=3;auto selected=field();replacement->backing->at(2)=selected;
    auto selected_storage=physical(selected,0x200000);
    f.create_ready=false;IgbEnumFieldDefaultsInvocation call;
    check(!call.resume(f.services).completed && f.selected_functions.size()==1,"Constructor did not retain its pending selection.");
    *f.cell=other;f.services->factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0});
    f.created_action=[&]{f.local->reference_word=0;};f.destroyed_action=[&]{f.target->fields=replacement;};f.create_ready=true;
    check(call.resume(f.services).completed && old_fields->count==3 && other->fields->count==0 && f.destroy_polls==1,
        "Defaults recaptured metadata or skipped the original batch release.");
    check(f.local->flag_26==0xa6 && selected->flag_26==0 && selected->offset==0x34 && selected->declaring_type->lock()==f.target &&
        *f.services->globals->at(0)->slot==selected && igb_bootstrap_read_byte(selected_storage,0x26)==0,
        "Selection did not reload the captured metadata's live registry after the batch.");
}
void wrapped_selection(){
    for(const std::uint32_t first:{0x80000000u,0xc0000000u,0x40000000u}){
        Fixture f;f.target->fields->count=std::bit_cast<std::int32_t>(first);
        f.created_action=[&]{f.target->fields->count=0;f.local->reference_word=0;};
        f.destroyed_action=[&]{f.target->fields->count=std::bit_cast<std::int32_t>(first+1u);};
        IgbEnumFieldDefaultsInvocation call;check(call.resume(f.services).completed && f.local->flag_26==0 && f.local->offset==0x34,
            "Enum defaults bounded an unwrapped index instead of the reached slot.");
        check(f.interns==1 && *f.services->globals->at(0)->slot==f.local,"Wrapped first index was not passed to the native schema.");
    }
    Fixture bad;bad.target->fields->count=-1;bad.created_action=[&]{bad.target->fields->count=0;};IgbEnumFieldDefaultsInvocation invalid;
    rejects([&]{invalid.resume(bad.services);},"Unrepresented wrapped physical slot was accepted.");
    check(bad.target->fields->count==1 && bad.local->flag_26==0xa6 && bad.interns==0,"Unavailable slot fault lost the completed batch prefix.");
}
void pending_flag_and_tables(){
    Fixture f;f.services->names->at(0).reset();IgbEnumFieldDefaultsInvocation call;
    check(!call.resume(f.services).completed && f.local->flag_26==0 && f.interns==0,"Missing schema name was reached before the flag store.");
    const auto captured_names=f.services->names;const auto captured_offsets=f.services->offsets;const auto captured_globals=f.services->globals;
    const auto before=f.allocations[0].storage->bytes->bytes;
    f.local->flag_26=0x77;igb_bootstrap_write_byte(f.allocations[0],0x26,0x77);
    f.services->names=std::make_shared<IgbFieldNameTable>();f.services->offsets=std::make_shared<IgbFieldOffsetTable>();f.services->globals=std::make_shared<IgbFieldGlobalTable>();
    captured_names->at(0)=std::make_shared<const std::string>("liveName");captured_offsets->at(0)=0x30;
    check(call.resume(f.services).completed && f.local->flag_26==0x77 && igb_bootstrap_read_byte(f.allocations[0],0x26)==0x77 &&
        f.local->name && *f.local->name=="liveName" && f.local->offset==0x30 && *captured_globals->at(0)->slot==f.local,
        "Pending schema recaptured table owners or replayed the flag store.");
    check(before[8+0x26]==0 && f.create_polls==1,"Schema continuation reran the field batch.");
    Fixture blocked;const auto retained_factory=blocked.services->schema->factory;blocked.services->schema->factory={};IgbEnumFieldDefaultsInvocation unresolved;
    check(!unresolved.resume(blocked.services).completed && !blocked.local && blocked.target->fields->count==0,"Missing constructor fabricated a field.");
    blocked.services->schema->factory=retained_factory;check(unresolved.resume(blocked.services).completed,"Late constructor binding did not resume native batch.");
}
void physical_fault_order(){
    for(unsigned mode=0;mode<5;++mode){Fixture f;f.created_action=[&]{auto& p=f.allocations[0];
            if(mode==0)p.storage->bytes->released=true;
            else if(mode==1)p.storage->bytes->bytes.resize(8+0x26);
            else if(mode==2)p.storage->bindings[0].field=field();
            else if(mode==3){f.local->physical_storage->storage.reset();}
            else p.storage->original_address=0xffffffd2u;};
        IgbEnumFieldDefaultsInvocation call;rejects([&]{call.resume(f.services);},"Malformed physical flag destination was accepted.");
        check(call.faulted() && f.target->fields->count==1 && f.local->reference_word==1 && f.local->flag_26==0xa6 && f.interns==0,
            "Flag destination was validated before batch completion or received later schema effects.");
        rejects([&]{call.resume(f.services);},"Faulted enum defaults resumed.");
    }
    Fixture clear;clear.services->names->at(0).reset();clear.created_action=[&]{clear.allocations[0].storage->bytes->initialized[8+0x26]=0;};
    IgbEnumFieldDefaultsInvocation writes;check(!writes.resume(clear.services).completed && clear.local->flag_26==0 &&
        clear.allocations[0].storage->bytes->initialized[8+0x26]==1,"Byte store required an initialized old flag value.");
}
void null_and_unknown_metadata(){
    IgbEnumFieldDefaultsInvocation missing;check(!missing.resume().completed && !missing.faulted(),"Unknown metadata was treated as represented null.");
    Fixture absent;absent.cell->reset();check(!missing.resume(absent.services).completed,"Unknown canonical metadata did not suspend.");
    *absent.cell=std::shared_ptr<IgbRuntimeMetaType>{};rejects([&]{missing.resume(absent.services);},"Represented null metadata skipped count dereference.");
    check(absent.create_polls==0,"Null metadata reached an unreached constructor.");
    Fixture no_registry;no_registry.target->fields.reset();IgbEnumFieldDefaultsInvocation invalid;
    rejects([&]{invalid.resume(no_registry.services);},"Null count registry was accepted.");
    check(no_registry.create_polls==0,"Null count registry reached batch construction.");
    Fixture selected_null;selected_null.created_action=[&]{selected_null.local->reference_word=0;};
    selected_null.destroyed_action=[&]{selected_null.target->fields->backing->at(0).reset();};IgbEnumFieldDefaultsInvocation null_field;
    rejects([&]{null_field.resume(selected_null.services);},"Selected null descriptor bypassed flag dereference.");
    check(selected_null.destroy_polls==1 && selected_null.interns==0,"Null descriptor fault changed callback order.");
}
void lifetime_adapters_and_recursion(){
    Fixture f;f.services->names->at(0).reset();std::weak_ptr<IgbObjectStorage> storage;
    {IgbEnumFieldDefaultsInvocation call;check(!call.resume(f.services).completed,"Lifetime case did not suspend in schema.");storage=f.allocations[0].storage;f.allocations.clear();}
    check(!storage.expired(),"Abandoned schema lost its selected physical allocation.");f.services->retained.clear();
    check(storage.expired(),"Retained physical allocation formed an ownership cycle.");
    Fixture adapter_fixture;bool delegated{};auto adapter=make_native_igb_enum_field_defaults_factory(adapter_fixture.services,[&](const auto&){delegated=true;return std::shared_ptr<IgbTypeRegistrationOperation>{};});
    IgbTypeRegistrationCall request;request.kind=IgbTypeRegistrationServiceKind::defaults;request.function=0x80031614;request.metadata=metadata();
    check(adapter(request)->resume().completed && request.metadata->fields->count==0,"Defaults adapter used a supplied receiver instead of the canonical global.");
    request.function=0x80031690;check(!adapter(request) && delegated,"Defaults adapter consumed the prototype callback.");
    auto services=std::make_shared<IgbEnumFieldDefaultsServices>();auto expired=make_native_igb_enum_field_defaults_factory(services);services.reset();request.function=0x80031614;
    rejects([&]{expired(request);},"Expired defaults adapter manufactured services.");
    Fixture recursive;IgbEnumFieldDefaultsInvocation call;recursive.created_action=[&]{try{call.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{call.resume(recursive.services);},"Caught recursive callback did not poison the outer defaults caller.");
    // The constructor swallowed the outer caller's exception and returned
    // success to the independent batch. That batch finishes append/release;
    // the defaults caller detects its poison when the batch returns, before
    // its own selection, flag store or schema. Do not erase that completed
    // nested-call prefix or pretend the batch shares the caller's fault flag.
    check(call.faulted() && !call.completed() && recursive.target->fields->count==1 &&
        recursive.target->fields->backing->at(0)==recursive.local && recursive.local->reference_word==1 &&
        igb_buffer_word(recursive.target->fields->backing_bytes,0)==0x100000 && recursive.create_polls==1 &&
        recursive.local->flag_26==0xa6 && igb_bootstrap_read_byte(recursive.allocations[0],0x26)==0xa6 &&
        recursive.interns==0 && recursive.buffer_allocations==0 && !recursive.services->globals->at(0)->slot->has_value(),
        "Caught recursion lost the completed batch prefix or reached later defaults stores.");
    rejects([&]{call.resume(recursive.services);},"Poisoned defaults caller resumed after its completed batch.");
    check(recursive.target->fields->count==1 && recursive.create_polls==1,"Poisoned defaults replayed its batch.");
    Fixture escaping;IgbEnumFieldDefaultsInvocation uncaught;
    escaping.created_action=[&]{uncaught.resume(escaping.services);};
    rejects([&]{uncaught.resume(escaping.services);},"Uncaught recursive exception did not leave the constructor immediately.");
    check(uncaught.faulted() && escaping.target->fields->count==0 && escaping.local->reference_word==1 &&
        escaping.local->flag_26==0xa6 && escaping.interns==0,
        "Uncaught recursive exception continued through native batch append.");
}
}
int main(){try{complete_schema();captured_receiver_and_live_registry();wrapped_selection();pending_flag_and_tables();physical_fault_order();
    null_and_unknown_metadata();lifetime_adapters_and_recursion();std::cout<<"igb_enum_field_defaults_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Enum-field defaults failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
