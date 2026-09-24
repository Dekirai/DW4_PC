#include "dw4/igb_list_defaults.hpp"
#include "dw4/igb_stream.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;using K=IgbListDefaultsServiceKind;using PK=IgbPointerListServiceKind;using FK=IgbFieldOperationKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& f,const char* why){++checks;try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<IgbScratchBuffer> bytes(unsigned size){auto b=std::make_shared<IgbScratchBuffer>();b->bytes.assign(size,0);b->initialized.assign(size,1);return b;}
std::shared_ptr<IgbRuntimeMetaType> metadata(){auto m=std::make_shared<IgbRuntimeMetaType>();m->fields=std::make_shared<IgbDecodeFieldRegistry>();
    m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(16);m->fields->backing_bytes=bytes(64);m->fields->backing_address=0x900000;return m;}
std::shared_ptr<IgbDecodeRuntimeField> field(std::uint32_t vt=0x80476088){auto f=std::make_shared<IgbDecodeRuntimeField>();f->vtable_word=vt;
    f->name_string=std::shared_ptr<IgbInternedString>{};f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};f->global_slot_address=0;
    f->default_bytes=IgbRuntimeFieldDefaultSource{bytes(4),0};f->default_bytes_address=0x700000;return f;}
std::shared_ptr<IgbObjectStorage> bind_physical(const std::shared_ptr<IgbDecodeRuntimeField>& field,std::uint32_t address,std::uint32_t size=0x50){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-4;storage->bytes=bytes(size+4);
    std::fill(storage->bytes->bytes.begin(),storage->bytes->bytes.end(),0xcc);storage->bindings.push_back({4,{},{},{},{},field});
    field->physical_storage=IgbFieldStorageBinding{storage,4};const IgbBootstrapPointer pointer{storage,4};
    igb_bootstrap_write_word(pointer,0,*field->vtable_word);igb_bootstrap_write_word(pointer,4,field->reference_word);
    igb_bootstrap_write_word(pointer,0x20,field->default_bytes_address.value_or(0));return storage;
}
struct Fixture {
    std::shared_ptr<IgbListDefaultsServices> services=std::make_shared<IgbListDefaultsServices>();
    std::shared_ptr<IgbRuntimeMetaType> children=metadata(),registry=metadata();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::shared_ptr<IgbDecodeRuntimeField> cloned,prototype=field(0x804762fc);
    std::vector<IgbListDefaultsCall> calls;
    std::vector<IgbPointerListCall> pointer_calls;
    std::function<void(const IgbPointerListCall&,IgbPointerListOperation&)> pointer_action;
    std::function<void(const IgbListDefaultsCall&)> action;
    bool ready=true;unsigned polls{},names{},queries{},capacity=64;
    Fixture(){
        services->lists=std::make_shared<IgbTypeListServices>();services->lists->children_metadata=std::make_shared<IgbRegistrationMetaSlot>(children);
        services->lists->registry_metadata=std::make_shared<IgbRegistrationMetaSlot>(registry);
        services->schema=std::make_shared<IgbFieldSchemaServices>();auto& schema=*services->schema;
        schema.types=std::make_shared<IgbTypeRegistrationServices>();schema.types->metadata_pool_handle=5;
        schema.pools=std::make_shared<IgbFilePoolsServices>();schema.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        schema.pools->memory_pools->selector=0;schema.pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        schema.dependencies=std::make_shared<IgbDependenciesServices>();schema.dependencies->globals=std::make_shared<SystemStageServices>();schema.dependencies->globals->string_pool=pool;
        schema.dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==IgbDependenciesServiceKind::intern_name){++names;auto header=std::make_shared<IgbInternedString>();header->value=igb_dependency_name(c.name);header->container=pool;out->string_result=header;}
            else if(c.kind!=IgbDependenciesServiceKind::release_string)throw std::runtime_error("Unexpected prepared string operation");return MessengerCallResult{true,0,{}};};return op;};
        schema.fields=std::make_shared<IgbFieldOperationsServices>();schema.fields->dependencies=schema.dependencies;schema.fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        // Prepared metadata arithmetic identities for physical schema+18 stores.
        schema.fields->pointer_lists->values={{0x600000,children,{}},{0x610000,registry,{}}};
        schema.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){pointer_calls.push_back(c);auto op=std::make_shared<IgbPointerListOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==PK::allocation_pool || c.kind==PK::object_pool)out->pool_result=pool;
            else if(c.kind==PK::allocation_size){++queries;out->size_result=capacity;}
            else if(c.kind==PK::reallocate){auto data=bytes(c.bytes);auto owners=std::make_shared<IgbDecodeFieldRegistry::Storage>(c.bytes/4);
                if(c.backing.fields)for(std::size_t i=0;i<owners->size() && i<c.backing.fields->size();++i)(*owners)[i]=(*c.backing.fields)[i];
                out->backing_result={data,0x920000,0,{},owners};}
            if(pointer_action)pointer_action(c,*out);return MessengerCallResult{true,0,{}};};return op;};
        services->values=std::make_shared<IgbFieldValuesServices>();services->values->fields=schema.fields;
        services->values->object_prototype=prototype;services->values->memory_reference_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->values->signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        schema.fields->factory=make_native_igb_field_values_factory(services->values,[this](const IgbFieldOperationCall& c){auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==FK::allocation_pool || c.kind==FK::object_pool)out->pool_result=pool;
            else if(c.kind==FK::allocation_size)return MessengerCallResult{true,4,{}};
            else throw std::runtime_error("Unexpected prepared default allocation");return MessengerCallResult{true,0,{}};};return op;});
        schema.factory=[this](const IgbFieldSchemaCall& c){check(c.kind==IgbFieldSchemaServiceKind::create_field,"Unexpected batch destruction");
            const auto index=created.size();check(c.function==(index==0?0x8002ac34u:0x8002e1acu),"Original two-field factory table differs");
            auto op=std::make_shared<IgbFieldSchemaOperation>();op->resume=[this,index,out=op.get()]{auto f=field(index==0?0x80476088u:0x80475b60u);created.push_back(f);
                out->field_result=f;out->field_address=0x100000u+static_cast<std::uint32_t>(index)*0x100;return MessengerCallResult{true,0,{}};};return op;};
        services->factory=[this](const IgbListDefaultsCall& c){calls.push_back(c);auto op=std::make_shared<IgbListDefaultsOperation>();op->resume=[this,c,out=op.get()]{
            ++polls;if(!ready)return MessengerCallResult{false,0,"prepared list dependency"};
            if(c.kind==K::register_object_field){check(c.function==0x800289f8,"Wrong object-field registration wrapper");services->values->object_prototype=prototype;}
            else if(c.kind==K::clone_field){check(c.virtual_slot==0x54 && c.argument==1,"Wrong clone virtual arguments");
                cloned=std::make_shared<IgbDecodeRuntimeField>(*c.field);cloned->reference_word=1;out->field_result=cloned;out->field_address=0x180000;}
            else check(c.function==0x80066e1c,"Wrong original field destructor");if(action)action(c);return MessengerCallResult{true,0,{}};};return op;};
        auto name=field(0x8047693c);name->offset=0x0c;services->name_descriptor=name;
        children->fields->count=2;for(unsigned i=0;i<2;++i){auto f=field();f->name=std::make_shared<const std::string>("base");f->name_string.reset();children->fields->backing->at(i)=f;}
    }
    void prepare_parent(){IgbListDefaultsInvocation parent(IgbListDefaultsEntry::children);check(parent.resume(services).completed,"Prepared parent defaults failed.");
        registry->fields->count=children->fields->count;*registry->fields->backing=*children->fields->backing;
        for(auto& f:*registry->fields->backing)if(f)++f->reference_word;}
};
void children_caller(){Fixture f;IgbListDefaultsInvocation call(IgbListDefaultsEntry::children);
    check(call.resume(f.services).completed && f.created.size()==2 && f.children->fields->count==4 && f.names==2,"Two local descriptors did not complete actual batch/schema.");
    const auto data=f.created[0],count=f.created[1];check(data->word_38==0x40 && data->element_field->lock()==f.prototype,"Array defaults lost element prototype or word38.");
    check(data->flag_34==1 && data->flag_35==1 && data->flag_40==1 && data->flag_41==1 && data->flag_42==0,"Array descriptor flags differ.");
    check(*data->name=="_data" && *count->name=="_count" && data->offset==8 && count->offset==12,"Original names or offsets differ.");
    check(data->global_slot_address==0x805619d8 && count->global_slot_address==0x805619dc &&
        *f.services->globals->at(0)->slot==data && *f.services->globals->at(1)->slot==count,"Original descriptor globals differ.");
    check(data->declaring_type->lock()==f.children && data->reference_word==1 && count->reference_word==1,"Schema ownership changed.");
    check(call.resume().completed && f.created.size()==2,"Completed list defaults replayed.");
}
void prototype_prefix(){Fixture f;f.services->values->object_prototype.reset();IgbListDefaultsInvocation call(IgbListDefaultsEntry::children);
    check(!call.resume(f.services).completed && f.created.size()==2 && !f.created[0]->word_38 && f.names==0,"Unknown prototype skipped required completed batch prefix.");
    f.services->values->object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};f.ready=false;
    check(!call.resume(f.services).completed && f.calls.size()==1 && f.calls[0].kind==K::register_object_field && !f.created[0]->element_field,"Cold prototype was fabricated ready.");
    f.services->lists->children_metadata->emplace(metadata());f.ready=true;
    check(call.resume(f.services).completed && f.created.size()==2 && f.calls.size()==1 && f.polls==2 && f.children->fields->count==4,"Pending getter replayed batch or lost captured receiver.");
    Fixture null;null.services->values->object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};null.action=[&](const auto&){null.services->values->object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};};
    IgbListDefaultsInvocation accepts_null(IgbListDefaultsEntry::children);check(accepts_null.resume(null.services).completed && null.created[0]->element_field && null.created[0]->element_field->expired(),"Getter's returned null was treated as missing registration.");
}
void internal_clone_and_replace(){Fixture f;f.prepare_parent();const auto original=f.created[0];IgbListDefaultsInvocation call(IgbListDefaultsEntry::registry);
    check(call.resume(f.services).completed && f.cloned && f.cloned!=original && f.calls.size()==1,"Internal defaults did not select exactly one clone.");
    check(f.calls[0].field==original && f.calls[0].vtable_word==0x80476088,"Clone did not use selected inherited field.");
    check(f.cloned->flag_42==1 && f.cloned->global_slot_address==0x805619d0 && *f.services->internal_data==f.cloned,"Clone flags/global publication differ.");
    check(f.registry->fields->backing->at(2)==f.cloned && f.registry->fields->count==4 && original->reference_word==1 && f.cloned->reference_word==1,"Replacement changed count or reference assignment.");
    check(igb_buffer_word(f.registry->fields->backing_bytes,8)==0x180000 && f.children->fields->backing->at(2)==original && *f.services->globals->at(0)->slot==original,"Replacement changed parent ownership or omitted raw pointer store.");
}
void pending_clone_and_old_release(){Fixture f;f.prepare_parent();f.ready=false;IgbListDefaultsInvocation call(IgbListDefaultsEntry::registry);
    check(!call.resume(f.services).completed && f.calls.size()==1 && !f.cloned,"Clone service did not suspend.");
    const auto old=f.created[0];old->name=std::make_shared<const std::string>("changed");
    f.ready=true;f.action=[&](const IgbListDefaultsCall& c){if(c.kind==K::clone_field){old->reference_word=0x80000001;f.ready=false;}};
    check(!call.resume(f.services).completed && f.calls.size()==2 && old->reference_word==0x80000000 && f.cloned->reference_word==2 && *f.services->internal_data==f.cloned,"Old destruction prefix/finalization ordering differs.");
    check(f.registry->fields->backing->at(2)==old,"Pending old destructor published replacement early.");
    auto new_backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(*f.registry->fields->backing);f.registry->fields->backing=new_backing;
    f.ready=true;check(call.resume(f.services).completed && f.calls.size()==2 && f.cloned->reference_word==1 && new_backing->at(2)==f.cloned,"Old destructor replayed or raw store ignored fresh backing.");
}
void replacement_growth_and_failures(){Fixture f;auto fresh=field();f.services->schema->fields->pointer_lists->values.push_back({0x180000,{},fresh});
    auto old=field();old->reference_word=4;f.registry->fields->count=7;f.registry->fields->backing->at(2)=old;f.capacity=4;
    IgbFieldReplaceInvocation replace(f.registry,2,fresh);check(replace.resume(f.services).completed && f.queries==3,"Original53C44/5383C capacity query sequence differs.");
    check(f.registry->fields->count==7 && f.registry->fields->backing_address==0x920000 && f.registry->fields->backing->at(2)==fresh && fresh->reference_word==2 && old->reference_word==3,"Native replacement growth/store changed references or count.");
    Fixture bad;auto replacement=field();auto global=std::make_shared<IgbRuntimeFieldSlot>();replacement->global_slot=global;
    IgbFieldReplaceInvocation null({},0,replacement);rejects([&]{null.resume(bad.services);},"Null metadata accepted after finalization.");
    check(*global==replacement && replacement->reference_word==1,"Null metadata failure skipped earlier finalization or retained too soon.");
    Fixture missing;missing.prepare_parent();missing.services->name_descriptor.reset();IgbListDefaultsInvocation unknown(IgbListDefaultsEntry::registry);
    check(!unknown.resume(missing.services).completed && missing.calls.empty(),"Missing canonical name descriptor was inferred.");
    missing.services->name_descriptor=std::shared_ptr<IgbDecodeRuntimeField>{};rejects([&]{unknown.resume(missing.services);},"Null name descriptor survived positive count lookup.");
    Fixture empty;empty.registry->fields->count=0;empty.services->name_descriptor=std::shared_ptr<IgbDecodeRuntimeField>{};IgbListDefaultsInvocation absent(IgbListDefaultsEntry::registry);
    rejects([&]{absent.resume(empty.services);},"Missing inherited data returned successful defaults.");check(empty.calls.empty(),"Missing field invoked an invented clone.");
}
void adapters_reentry_and_alias(){Fixture f;f.prepare_parent();auto adapter=make_native_igb_list_defaults_factory(f.services);IgbTypeRegistrationCall request;
    request.kind=IgbTypeRegistrationServiceKind::defaults;request.function=0x8002ddd4;request.metadata=metadata();
    check(adapter(request)->resume().completed && request.metadata->fields->count==0,"Adapter used generic receiver instead of canonical list metadata.");
    request.function=0x8002a164;check(!adapter(request),"List adapter swallowed unrelated defaults.");
    Fixture alias;auto value=field();value->reference_word=0xffffffffu;alias.registry->fields->backing->at(0)=value;
    alias.services->schema->fields->pointer_lists->values.push_back({0x110000,{},value});IgbFieldReplaceInvocation self(alias.registry,0,value);
    check(self.resume(alias.services).completed && value->reference_word==0xffffffffu && alias.calls.empty(),"Aliased replacement skipped full-word retain/release wrapping.");
    Fixture recursive;recursive.prepare_parent();IgbListDefaultsInvocation invocation(IgbListDefaultsEntry::registry);
    recursive.action=[&](const auto&){try{invocation.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{invocation.resume(recursive.services);},"Swallowed recursive defaults failed to poison caller.");
    check(invocation.faulted() && !*recursive.services->internal_data,"Recursive failure published unreached cloned global.");
}
void physical_array_publication(){
    Fixture f;std::vector<std::shared_ptr<IgbObjectStorage>> allocations;const auto factory=f.services->schema->factory;
    f.services->schema->factory=[&](const IgbFieldSchemaCall& c){auto op=factory(c);const auto run=op->resume;
        op->resume=[&,run,out=op.get(),bound=false]()mutable{const auto status=run();if(status.completed && !bound){
            allocations.push_back(bind_physical(out->field_result,*out->field_address));bound=true;}return status;};return op;};
    IgbListDefaultsInvocation call(IgbListDefaultsEntry::children);const auto missing=call.resume(f.services);
    check(!missing.completed && missing.diagnostic.find("arithmetic address")!=std::string::npos && f.created.size()==2 && f.names==0,
        "Physical array publication fabricated its unbound element pointer or skipped completed batch creation.");
    const IgbBootstrapPointer array{allocations[0],4};const auto captured=f.prototype;
    check(igb_bootstrap_read_word(array,0x3c)==0xcccccccc && !f.created[0]->element_field && !f.created[0]->word_38,
        "Missing element address published canonical or physical array configuration early.");
    f.services->values->object_prototype=field(0x804762fc);f.services->schema->fields->pointer_lists->values.push_back({0x660000,{},captured});
    check(call.resume(f.services).completed && f.created.size()==2 && f.created[0]->element_field->lock()==captured,
        "Late element address replayed creation or reloaded the already captured prototype.");
    check(igb_bootstrap_read_word(array,0x18)==0x600000 && igb_bootstrap_read_word(array,0x1c)==0x805619d8 &&
        igb_bootstrap_read_word(array,8)==8,"Physical array schema lost its declaring metadata, global cell or offset.");
    check(igb_bootstrap_read_word(array,0x3c)==0x660000 && igb_bootstrap_read_word(array,0x38)==0x40,
        "Configured array element and element size were not published physically.");
    for(const auto offset:{0x34u,0x35u,0x40u,0x41u})check(igb_bootstrap_read_byte(array,offset)==1,"Array flag physical publication differs.");
    check(igb_bootstrap_read_byte(array,0x42)==0 && igb_bootstrap_read_byte(array,0x36)==0xcc && igb_bootstrap_read_byte(array,0x43)==0xcc,
        "Array stores changed their final flag or untouched padding.");
    Fixture physical;std::vector<std::shared_ptr<IgbObjectStorage>> storage;const auto prepared=physical.services->schema->factory;
    auto prototype=bind_physical(physical.prototype,0x670000,0x44);
    physical.services->schema->factory=[&](const auto& c){auto op=prepared(c);const auto run=op->resume;op->resume=[&,run,out=op.get()]{
        const auto result=run();if(result.completed)storage.push_back(bind_physical(out->field_result,*out->field_address));return result;};return op;};
    IgbListDefaultsInvocation direct(IgbListDefaultsEntry::children);check(direct.resume(physical.services).completed &&
        igb_bootstrap_read_word({storage[0],4},0x3c)==0x670000,"Validated physical prototype identity did not supply its actual address.");
    check(igb_bootstrap_read_word({prototype,4},4)==1 && physical.prototype->reference_word==1,
        "Array element publication introduced ownership for its original nonowning reference.");
}
void physical_array_failure_prefix(){
    Fixture f;std::vector<std::shared_ptr<IgbObjectStorage>> allocations;const auto factory=f.services->schema->factory;
    f.services->schema->fields->pointer_lists->values.push_back({0x660000,{},f.prototype});
    f.services->schema->factory=[&](const auto& c){auto op=factory(c);const auto run=op->resume;op->resume=[&,run,out=op.get()]{
        const auto result=run();if(result.completed)allocations.push_back(bind_physical(out->field_result,*out->field_address,0x40));return result;};return op;};
    IgbListDefaultsInvocation call(IgbListDefaultsEntry::children);rejects([&]{call.resume(f.services);},"Short physical array descriptor accepted out-of-range flag stores.");
    const IgbBootstrapPointer array{allocations[0],4};check(call.faulted() && f.names==0 &&
        igb_bootstrap_read_word(array,0x3c)==0x660000 && igb_bootstrap_read_word(array,0x38)==0x40 &&
        igb_bootstrap_read_byte(array,0x34)==1 && igb_bootstrap_read_byte(array,0x35)==1,
        "Array storage fault lost the original element, size and first flag store prefix.");
    check(f.created[0]->element_field->lock()==f.prototype && f.created[0]->word_38==0x40 && f.created[0]->flag_34==1 &&
        f.created[0]->flag_35==1 && !f.created[0]->flag_40 && !f.created[0]->flag_41 && !f.created[0]->flag_42,
        "Failed physical array store published later canonical flags.");
}
void physical_clone_and_references(){
    Fixture f;f.prepare_parent();const auto old=f.created[0];old->reference_word=1;auto original=bind_physical(old,0x100000);
    std::shared_ptr<IgbObjectStorage> clone;
    f.action=[&](const IgbListDefaultsCall& c){if(c.kind==K::clone_field){clone=bind_physical(f.cloned,0x180000);f.ready=false;}};
    IgbListDefaultsInvocation call(IgbListDefaultsEntry::registry);check(!call.resume(f.services).completed && clone && old->reference_word==0 && f.cloned->reference_word==2,
        "Physical clone replacement did not suspend after retain and old release.");
    const IgbBootstrapPointer copied{clone,4};check(igb_bootstrap_read_byte(copied,0x42)==1 && igb_bootstrap_read_word(copied,0x1c)==0x805619d0 &&
        igb_bootstrap_read_word(copied,4)==2 && igb_bootstrap_read_word({original,4},4)==0,
        "Clone flags, descriptor-global or replacement reference words were not physically published in order.");
    check(f.registry->fields->backing->at(2)==old && *f.services->internal_data==f.cloned,
        "Pending physical destruction changed the backing before native finalization/publication.");
    f.ready=true;check(call.resume(f.services).completed && f.cloned->reference_word==1 && igb_bootstrap_read_word(copied,4)==1 &&
        igb_bootstrap_read_word({original,4},4)==0 && f.registry->fields->backing->at(2)==f.cloned,
        "Resumption repeated physical reference changes or omitted the creator release.");
    check(igb_bootstrap_read_byte(copied,0x43)==0xcc,"Physical clone flag publication changed padding.");
    Fixture alias;auto value=field();value->reference_word=0xffffffffu;auto allocation=bind_physical(value,0x190000);
    alias.registry->fields->backing->at(0)=value;alias.services->schema->fields->pointer_lists->values.push_back({0x190000,{},value});
    IgbFieldReplaceInvocation self(alias.registry,0,value);check(self.resume(alias.services).completed && value->reference_word==0xffffffffu &&
        igb_bootstrap_read_word({allocation,4},4)==0xffffffffu,"Physical self-replacement lost full-word reference wrap.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_list_defaults_tests");
    children_caller();prototype_prefix();internal_clone_and_replace();pending_clone_and_old_release();replacement_growth_and_failures();adapters_reentry_and_alias();
    physical_array_publication();physical_array_failure_prefix();physical_clone_and_references();
    std::cout<<"igb_list_defaults_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"List defaults failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
