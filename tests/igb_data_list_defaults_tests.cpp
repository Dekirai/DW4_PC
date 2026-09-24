#include "dw4/igb_data_list_defaults.hpp"
#include "dw4/igb_stream.hpp"

#include <algorithm>
#include <bit>
#include <iostream>

namespace {
using namespace dw4;using K=IgbListDefaultsServiceKind;using FK=IgbFieldOperationKind;using PK=IgbPointerListServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& fn,const char* message){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(message);}
std::shared_ptr<IgbScratchBuffer> bytes(std::size_t count,std::uint8_t value=0){auto result=std::make_shared<IgbScratchBuffer>();
    result->bytes.assign(count,value);result->initialized.assign(count,1);return result;}
std::shared_ptr<IgbRuntimeMetaType> metadata(std::uint32_t address){auto m=std::make_shared<IgbRuntimeMetaType>();m->fields=std::make_shared<IgbDecodeFieldRegistry>();
    m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(16);m->fields->backing_bytes=bytes(64);m->fields->backing_address=address;return m;}
std::shared_ptr<IgbDecodeRuntimeField> field(std::uint32_t table=0x80476088){auto f=std::make_shared<IgbDecodeRuntimeField>();f->vtable_word=table;
    f->name_string=std::shared_ptr<IgbInternedString>{};f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};f->global_slot_address=0;
    f->default_bytes=IgbRuntimeFieldDefaultSource{bytes(4),0};f->default_bytes_address=0x700000;
    f->word_38=0x55667788;f->flag_34=std::uint8_t{0xa1};f->flag_42=std::uint8_t{0xa2};return f;}
IgbBootstrapPointer physical(const std::shared_ptr<IgbDecodeRuntimeField>& field,std::uint32_t address,std::uint32_t size=0x50){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-8;storage->bytes=bytes(size+8,0xcc);
    storage->bindings.push_back({8,{},{},{},{},field});field->physical_storage=IgbFieldStorageBinding{storage,8};
    IgbBootstrapPointer p{storage,8};igb_bootstrap_write_word(p,0,*field->vtable_word);igb_bootstrap_write_word(p,4,field->reference_word);
    igb_bootstrap_write_word(p,0x20,*field->default_bytes_address);return p;
}
struct Fixture {
    static constexpr std::uint32_t data_address=0x620000,object_address=0x630000;
    std::shared_ptr<IgbDataListDefaultsServices> services=std::make_shared<IgbDataListDefaultsServices>();
    std::shared_ptr<IgbRuntimeMetaType> data=metadata(0x900000),object=metadata(0x910000);
    std::shared_ptr<IgbDecodeRuntimeField> byte_prototype=field(0x80300000),object_prototype=field(0x804762fc),clone;
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<IgbBootstrapPointer> allocations;
    std::vector<IgbListDefaultsCall> calls;std::vector<std::uint32_t> callback_functions;
    std::function<void(const IgbListDefaultsCall&)> action;std::function<void(std::uint32_t)> registration_action;
    bool ready=true,registration_ready=true,with_physical{};unsigned polls{},registration_polls{},names{},physical_size=0x50;
    Fixture(){
        services->registration=std::make_shared<IgbDataListRegistrationServices>();
        services->registration->data_metadata=std::make_shared<IgbRegistrationMetaSlot>(data);
        services->registration->object_metadata=std::make_shared<IgbRegistrationMetaSlot>(object);
        services->lists=std::make_shared<IgbListDefaultsServices>();services->lists->schema=std::make_shared<IgbFieldSchemaServices>();
        auto& s=*services->lists->schema;s.types=std::make_shared<IgbTypeRegistrationServices>();s.types->metadata_pool_handle=5;
        s.pools=std::make_shared<IgbFilePoolsServices>();s.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        s.pools->memory_pools->selector=0;s.pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        s.dependencies=std::make_shared<IgbDependenciesServices>();s.dependencies->globals=std::make_shared<SystemStageServices>();s.dependencies->globals->string_pool=pool;
        s.dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==IgbDependenciesServiceKind::intern_name){++names;auto value=std::make_shared<IgbInternedString>();value->value=igb_dependency_name(c.name);value->container=pool;out->string_result=value;}
            else check(c.kind==IgbDependenciesServiceKind::release_string,"Unexpected prepared name operation.");return MessengerCallResult{true,0,{}};};return op;};
        s.fields=std::make_shared<IgbFieldOperationsServices>();s.fields->dependencies=s.dependencies;s.fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        // Prepared metadata identities are distinct from their pointer-array addresses.
        s.fields->pointer_lists->values.push_back({data_address,data,{}});
        s.fields->pointer_lists->values.push_back({object_address,object,{}});
        s.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==PK::allocation_pool || c.kind==PK::object_pool)out->pool_result=pool;
            else{check(c.kind==PK::allocation_size,"Unexpected prepared list growth.");out->size_result=64;}return MessengerCallResult{true,0,{}};};return op;};
        services->lists->values=std::make_shared<IgbFieldValuesServices>();services->lists->values->fields=s.fields;
        services->lists->values->object_prototype=object_prototype;
        services->lists->values->signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->lists->values->memory_reference_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        *services->unsigned_char_prototype=byte_prototype;
        s.fields->factory=make_native_igb_field_values_factory(services->lists->values,[this](const IgbFieldOperationCall& c){
            auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,out=op.get()]{
                if(c.kind==FK::allocation_pool || c.kind==FK::object_pool)out->pool_result=pool;
                else{check(c.kind==FK::allocation_size,"Unexpected prepared default allocation.");return MessengerCallResult{true,4,{}};}
                return MessengerCallResult{true,0,{}};};return op;});
        // Constructor leaves are prepared here; production composition uses
        // the real enabled field registration and reflected factory chain.
        s.factory=[this](const IgbFieldSchemaCall& c){check(c.kind==IgbFieldSchemaServiceKind::create_field,"Unexpected batch destructor.");
            const auto index=created.size();check(index<3 && c.function==(index<2?0x8002e1acu:0x8002ac34u),"Data-list constructor table differs.");
            auto op=std::make_shared<IgbFieldSchemaOperation>();op->resume=[this,index,out=op.get()]{auto f=field(index<2?0x80475b60u:0x80476088u);created.push_back(f);
                out->field_result=f;out->field_address=0x100000+static_cast<std::uint32_t>(index)*0x100;
                if(with_physical)allocations.push_back(physical(f,*out->field_address,physical_size));return MessengerCallResult{true,0,{}};};return op;};
        services->lists->factory=[this](const IgbListDefaultsCall& c){calls.push_back(c);auto op=std::make_shared<IgbListDefaultsOperation>();op->resume=[this,c,out=op.get()]{
            ++polls;if(!ready)return MessengerCallResult{false,0,"prepared clone/destructor"};
            if(c.kind==K::clone_field){check(c.virtual_slot==0x54 && c.argument==1,"Data-list clone arguments differ.");
                clone=std::make_shared<IgbDecodeRuntimeField>(*c.field);clone->physical_storage.reset();clone->reference_word=1;
                out->field_result=clone;out->field_address=0x180000;
                if(with_physical)allocations.push_back(physical(clone,*out->field_address,physical_size));
            }else check(c.kind==K::destroy_field && c.function==0x80066e1c,"Unexpected clone service call.");
            if(action)action(c);return MessengerCallResult{true,0,{}};};return op;};
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();
        services->callbacks->factory=[this](const IgbRegistrationCallbacksCall& c){check(c.kind==IgbRegistrationCallbacksServiceKind::invoke_function,"Unexpected callback cleanup.");
            callback_functions.push_back(c.function);auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[this,c]{
                ++registration_polls;if(!registration_ready)return MessengerCallResult{false,0,"prepared prototype registration"};
                if(c.function==0x80023234)*services->unsigned_char_prototype=byte_prototype;
                else{check(c.function==0x80028a20,"Prototype wrapper selected another registration body.");services->lists->values->object_prototype=object_prototype;}
                if(registration_action)registration_action(c.function);return MessengerCallResult{true,0,{}};};return op;};
        auto name=field(0x8047693c);name->offset=0x0c;services->lists->name_descriptor=name;
        data->fields->count=2;for(unsigned i=0;i<2;++i){auto base=field();base->name=std::make_shared<const std::string>("base");base->name_string.reset();data->fields->backing->at(i)=base;}
    }
    void prepare_parent(){IgbDataListDefaultsInvocation parent(IgbDataListKind::data_list);check(parent.resume(services).completed,"Prepared DataList schema failed.");
        object->fields->count=data->fields->count;*object->fields->backing=*data->fields->backing;
        for(auto& entry:*object->fields->backing)if(entry)++entry->reference_word;}
    void bind_prototypes(){services->lists->schema->fields->pointer_lists->values.push_back({0x660000,{},byte_prototype});
        services->lists->schema->fields->pointer_lists->values.push_back({0x670000,{},object_prototype});}
};
void data_schema(){Fixture f;IgbDataListDefaultsInvocation call(IgbDataListKind::data_list);
    check(call.resume(f.services).completed && f.created.size()==3 && f.data->fields->count==5 && f.names==3,"DataList did not append/configure exactly three fields.");
    for(std::size_t i=0;i<3;++i){const auto d=f.created[i];check(d->name && *d->name==**f.services->names->at(i) && d->offset==8+i*4 &&
        d->declaring_type->lock()==f.data && d->reference_word==1 && *f.services->globals->at(i)->slot==d &&
        d->global_slot_address==0x80561d40+i*4,"DataList schema names, offsets, globals or ownership differ.");}
    const auto data=f.created[2];check(data->element_field->lock()==f.byte_prototype && data->flag_35==1 && data->flag_40==1 && data->flag_41==1 &&
        data->word_38==0x55667788 && data->flag_34==0xa1 && data->flag_42==0xa2,"DataList changed members not stored by33AA4.");
    check(f.callback_functions.empty() && call.resume().completed && f.created.size()==3,"Ready prototype getter registered again or completed defaults replayed.");
}
void wrapped_first_index(){Fixture f;f.with_physical=true;f.bind_prototypes();
    f.data->fields->count=std::bit_cast<std::int32_t>(0x80000000u);
    IgbDataListDefaultsInvocation call(IgbDataListKind::data_list);
    check(call.resume(f.services).completed && f.created.size()==3 && f.names==3 &&
        f.data->fields->count==std::bit_cast<std::int32_t>(0x80000003u),
        "Wrapped original field index was rejected before selecting its represented physical slot.");
    for(std::size_t i=0;i<3;++i)check(f.data->fields->backing->at(i)==f.created[i] &&
        igb_buffer_word(f.data->fields->backing_bytes,i*4)==0x100000+i*0x100 &&
        f.created[i]->offset==8+i*4 && *f.services->globals->at(i)->slot==f.created[i] &&
        igb_bootstrap_read_word(f.allocations[i],0x18)==Fixture::data_address &&
        igb_bootstrap_read_word(f.allocations[i],8)==8+i*4 &&
        igb_bootstrap_read_word(f.allocations[i],0x1c)==f.services->globals->at(i)->address,
        "Wrapping batch/schema field offsets lost physical publication or canonical configuration.");
    check(f.created[2]->element_field->lock()==f.byte_prototype && igb_bootstrap_read_word(f.allocations[2],0x3c)==0x660000 &&
        f.created[2]->flag_35==1 && f.created[2]->flag_40==1 && f.created[2]->flag_41==1,
        "Wrapped first+2 lookup did not select the actual third descriptor for prototype configuration.");
}
void prototype_getters(){
    for(const auto kind:{IgbDataListPrototypeKind::unsigned_char,IgbDataListPrototypeKind::object}){
        Fixture f;auto cell=kind==IgbDataListPrototypeKind::unsigned_char?f.services->unsigned_char_prototype:
            std::shared_ptr<IgbRuntimeFieldSlot>(f.services->lists->values,&f.services->lists->values->object_prototype);
        cell->reset();IgbDataListPrototypeInvocation call(kind);check(!call.resume(f.services).completed && f.callback_functions.empty(),"Unknown prototype became a null cache hit.");
        *cell=std::shared_ptr<IgbDecodeRuntimeField>{};f.registration_ready=false;
        check(!call.resume(f.services).completed && f.callback_functions.size()==1 && f.registration_polls==1,"Null prototype skipped its actual wrapper.");
        check(!call.resume(f.services).completed && f.callback_functions.size()==1 && f.registration_polls==2,"Pending prototype wrapper reselected its function.");
        f.registration_ready=true;f.registration_action=[cell](auto){*cell=std::shared_ptr<IgbDecodeRuntimeField>{};};
        check(call.resume(f.services).completed && !call.result() && f.callback_functions.front()==(kind==IgbDataListPrototypeKind::unsigned_char?0x80023234u:0x80028a20u),
            "Prototype getter did not reload its distinct canonical null result after registration.");
        rejects([]{IgbDataListPrototypeInvocation bad(static_cast<IgbDataListPrototypeKind>(9));},"Invalid prototype kind accepted.");
    }
    Fixture captured;captured.services->unsigned_char_prototype->reset();IgbDataListDefaultsInvocation call(IgbDataListKind::data_list);
    check(!call.resume(captured.services).completed && captured.created.size()==3 && captured.names==0,"Unknown prototype skipped the completed batch prefix.");
    auto replacement=metadata(0x950000);*captured.services->registration->data_metadata=replacement;
    captured.services->lists->schema->fields->pointer_lists->values.push_back({0x640000,replacement,{}});
    *captured.services->unsigned_char_prototype=captured.byte_prototype;
    check(call.resume(captured.services).completed && captured.data->fields->count==5 && replacement->fields->count==0 && captured.created.size()==3,
        "Prototype suspension lost the captured metadata receiver or replayed the batch.");
}
void physical_data_prefix(){Fixture f;f.with_physical=true;IgbDataListDefaultsInvocation call(IgbDataListKind::data_list);
    check(!call.resume(f.services).completed && f.created.size()==3 && f.names==0,"Physical DataList invented the byte prototype's arithmetic address.");
    const auto data=f.created[2];const auto p=f.allocations[2];const auto captured=f.byte_prototype;
    check(igb_bootstrap_read_word(p,0x3c)==0xcccccccc && !data->element_field && !data->flag_35,"Missing prototype address published later stores.");
    *f.services->unsigned_char_prototype=field();f.services->lists->schema->fields->pointer_lists->values.push_back({0x660000,{},captured});
    check(call.resume(f.services).completed && data->element_field->lock()==captured && igb_bootstrap_read_word(p,0x3c)==0x660000 &&
        igb_bootstrap_read_byte(p,0x35)==1 && igb_bootstrap_read_byte(p,0x40)==1 && igb_bootstrap_read_byte(p,0x41)==1,
        "DataList physical stores lost the captured prototype or their exact offsets.");
    check(igb_bootstrap_read_word(p,0x38)==0xcccccccc && igb_bootstrap_read_byte(p,0x34)==0xcc && igb_bootstrap_read_byte(p,0x42)==0xcc,
        "DataList defaults touched unstored size or flags.");
    Fixture short_field;short_field.with_physical=true;short_field.physical_size=0x40;short_field.bind_prototypes();IgbDataListDefaultsInvocation partial(IgbDataListKind::data_list);
    rejects([&]{partial.resume(short_field.services);},"Short DataList descriptor accepted byte+40.");
    check(partial.faulted() && short_field.names==0 && short_field.created[2]->element_field->lock()==short_field.byte_prototype &&
        short_field.created[2]->flag_35==1 && !short_field.created[2]->flag_40 && !short_field.created[2]->flag_41 &&
        igb_bootstrap_read_word(short_field.allocations[2],0x3c)==0x660000 && igb_bootstrap_read_byte(short_field.allocations[2],0x35)==1,
        "Faulting DataList physical store lost its committed prefix or published later flags.");
}
void object_specialization(){Fixture f;f.prepare_parent();const auto source=f.created[2];IgbDataListDefaultsInvocation call(IgbDataListKind::nonref_object);
    check(call.resume(f.services).completed && f.calls.size()==1 && f.calls[0].field==source && f.calls[0].vtable_word==0x80476088,
        "Specialization did not select the inherited data descriptor and its actual clone virtual.");
    check(f.clone && f.clone!=source && f.clone->element_field->lock()==f.object_prototype && f.clone->word_38==0 && f.clone->flag_42==0 &&
        f.clone->flag_34==0xa1 && f.clone->flag_35==1 && f.clone->flag_40==1 && f.clone->flag_41==1,
        "Non-reference-counted object specialization changed the wrong memory-field members.");
    check(f.object->fields->backing->at(4)==f.clone && f.data->fields->backing->at(4)==source && source->reference_word==1 && f.clone->reference_word==1 &&
        *f.services->nonref_data==f.clone && *f.services->globals->at(2)->slot==source && f.clone->global_slot_address==0x80561734 &&
        igb_buffer_word(f.object->fields->backing_bytes,16)==0x180000,"Specialization replacement/global publication changed parent ownership or skipped physical storage.");
}
void retained_clone_and_physical_release(){Fixture f;f.bind_prototypes();f.with_physical=true;f.prepare_parent();f.ready=false;
    IgbDataListDefaultsInvocation call(IgbDataListKind::nonref_object);check(!call.resume(f.services).completed && f.calls.size()==1 && !f.clone,"Clone boundary did not retain its selection.");
    const auto source=f.created[2];source->reference_word=1;igb_bootstrap_write_word(f.allocations[2],4,1);
    f.ready=true;f.action=[&](const auto& c){if(c.kind==K::clone_field)f.ready=false;};
    check(!call.resume(f.services).completed && f.calls.size()==2 && source->reference_word==0 && f.clone->reference_word==2,
        "Replacement did not retain clone and decrement old descriptor before its pending destructor.");
    const auto p=f.allocations.back();check(igb_bootstrap_read_word(p,0x3c)==0x670000 && igb_bootstrap_read_word(p,0x38)==0 &&
        igb_bootstrap_read_byte(p,0x42)==0 && igb_bootstrap_read_word(p,0x1c)==0x80561734 && igb_bootstrap_read_word(p,4)==2 &&
        *f.services->nonref_data==f.clone && f.object->fields->backing->at(4)==source,"Physical specialization stores or pre-destruction finalization order differ.");
    f.ready=true;check(call.resume(f.services).completed && f.object->fields->backing->at(4)==f.clone && f.clone->reference_word==1 &&
        igb_bootstrap_read_word(p,4)==1 && igb_bootstrap_read_word(f.allocations[2],4)==0,"Resumed replacement repeated reference changes or skipped creator release.");
}
void null_clone_and_failures(){
    Fixture null;null.prepare_parent();null.services->lists->values->object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    null.services->lists->factory=[](const IgbListDefaultsCall&){auto op=std::make_shared<IgbListDefaultsOperation>();op->resume=[] {return MessengerCallResult{true,0,{}};};return op;};
    IgbDataListDefaultsInvocation call(IgbDataListKind::nonref_object);rejects([&]{call.resume(null.services);},"Null cloned descriptor was accepted.");
    check(null.callback_functions==std::vector<std::uint32_t>{0x80028a20} && !*null.services->nonref_data,
        "Null clone fault occurred before original prototype getter or published a later global.");
    Fixture absent;absent.object->fields->count=0;absent.services->lists->name_descriptor=std::shared_ptr<IgbDecodeRuntimeField>{};
    IgbDataListDefaultsInvocation missing(IgbDataListKind::nonref_object);rejects([&]{missing.resume(absent.services);},"Missing inherited data was accepted.");
    check(absent.calls.empty(),"Absent data descriptor invoked an invented clone.");
    Fixture moved;moved.with_physical=true;IgbDataListDefaultsInvocation pending(IgbDataListKind::data_list);
    check(!pending.resume(moved.services).completed,"Physical rebinding fixture did not suspend.");
    const auto before=moved.allocations[2].storage->bytes->bytes;moved.created[2]->physical_storage.reset();
    rejects([&]{pending.resume(moved.services);},"Pending data descriptor lost its physical identity without failing.");
    check(moved.allocations[2].storage->bytes->bytes==before,"Invalid physical descriptor received additional writes.");
}
void getter_precedes_destination_fault(){
    for(const auto kind:{IgbDataListKind::data_list,IgbDataListKind::nonref_object})for(const bool expired:{false,true}){
        Fixture f;
        const auto damage=[&](const std::shared_ptr<IgbDecodeRuntimeField>& descriptor){
            if(expired)descriptor->physical_storage=IgbFieldStorageBinding{{},8};
            else f.allocations.back().storage->bindings.front().field=field();
        };
        if(kind==IgbDataListKind::data_list){
            f.with_physical=true;*f.services->unsigned_char_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
            const auto factory=f.services->lists->schema->factory;
            f.services->lists->schema->factory=[&,factory](const IgbFieldSchemaCall& request){auto op=factory(request);const auto run=op->resume;
                op->resume=[&,run,out=op.get()]{const auto status=run();if(status.completed && f.created.size()==3)damage(out->field_result);return status;};return op;};
        }else{
            f.prepare_parent();f.with_physical=true;f.services->lists->values->object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
            f.action=[&](const IgbListDefaultsCall& request){if(request.kind==K::clone_field)damage(f.clone);};
        }
        const auto before_names=f.names;f.registration_ready=false;IgbDataListDefaultsInvocation call(kind);
        check(!call.resume(f.services).completed && !call.faulted() && f.callback_functions.size()==1 && f.registration_polls==1,
            "Malformed destination storage faulted before the original prototype registration call.");
        const auto destination=f.allocations.back();const auto before=destination.storage->bytes->bytes;
        check(!call.resume(f.services).completed && f.callback_functions.size()==1 && f.registration_polls==2 && !call.faulted(),
            "Pending prototype getter validated later destination storage or reselected registration.");
        f.registration_ready=true;rejects([&]{call.resume(f.services);},"Malformed destination storage accepted the first element store.");
        check(call.faulted() && f.registration_polls==3 && destination.storage->bytes->bytes==before && f.names==before_names &&
            f.callback_functions.front()==(kind==IgbDataListKind::data_list?0x80023234u:0x80028a20u) &&
            (kind==IgbDataListKind::data_list?*f.services->unsigned_char_prototype==f.byte_prototype:
                f.services->lists->values->object_prototype==f.object_prototype),
            "Destination fault lost completed getter side effects, changed physical bytes or reached later schema work.");
    }
}
void adapters_and_recursion(){Fixture f;auto adapter=make_native_igb_data_list_defaults_factory(f.services);IgbTypeRegistrationCall request;
    request.kind=IgbTypeRegistrationServiceKind::defaults;request.function=0x80033aa4;request.metadata=metadata(0x960000);
    check(adapter(request)->resume().completed && request.metadata->fields->count==0 && f.data->fields->count==5,"Adapter used generic metadata instead of the canonical global.");
    request.function=0x80023940;check(!adapter(request),"Data-list adapter consumed another class's defaults.");
    rejects([]{IgbDataListDefaultsInvocation bad(IgbDataListKind::thread_list);},"Defaults-free thread list accepted a fabricated defaults body.");
    Fixture recursive;recursive.prepare_parent();IgbDataListDefaultsInvocation call(IgbDataListKind::nonref_object);
    recursive.action=[&](const auto&){try{call.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{call.resume(recursive.services);},"Caught recursive defaults failure did not poison its outer continuation.");
    check(call.faulted() && !*recursive.services->nonref_data,"Recursive failure published an unreached descriptor global.");
}
}
int main(){try{data_schema();wrapped_first_index();prototype_getters();physical_data_prefix();object_specialization();retained_clone_and_physical_release();null_clone_and_failures();getter_precedes_destination_fault();adapters_and_recursion();
    std::cout<<"igb_data_list_defaults_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Data-list defaults failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
