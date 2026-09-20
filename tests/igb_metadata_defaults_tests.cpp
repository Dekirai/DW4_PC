#include "dw4/igb_metadata_defaults.hpp"
#include "dw4/igb_stream.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<IgbRuntimeMetaType> metadata(){auto m=std::make_shared<IgbRuntimeMetaType>();
    m->fields=std::make_shared<IgbDecodeFieldRegistry>();m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(64);
    m->fields->backing_bytes=std::make_shared<IgbScratchBuffer>();m->fields->backing_bytes->bytes.assign(256,0);
    m->fields->backing_bytes->initialized.assign(256,1);m->fields->backing_address=0x900000;return m;}
struct Fixture {
    std::shared_ptr<IgbMetadataDefaultsServices> services=std::make_shared<IgbMetadataDefaultsServices>();
    std::shared_ptr<IgbRuntimeMetaType> meta=metadata(),field_list=metadata(),metadata_list=metadata(),object=metadata();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<std::uint32_t> functions;
    std::vector<IgbFieldOperationCall> scalar_calls;
    std::function<void(const IgbFieldOperationCall&)> scalar_action;
    std::function<void(const IgbMetadataDefaultsCall&)> string_action;
    unsigned field_polls{},scalar_polls{},string_selections{},string_polls{},interns{};
    bool fields_ready=true,scalar_ready=true,string_ready=true;
    Fixture(){
        services->metadata=std::make_shared<IgbMetadataFactoryServices>();services->metadata->metadata=meta;
        *services->field_list_metadata=field_list;*services->metadata_list_metadata=metadata_list;
        services->object_metadata=std::make_shared<IgbRegistrationMetaSlot>(object);
        services->schema=std::make_shared<IgbFieldSchemaServices>();auto& schema=*services->schema;
        schema.types=std::make_shared<IgbTypeRegistrationServices>();schema.types->metadata_pool_handle=5;
        schema.pools=std::make_shared<IgbFilePoolsServices>();schema.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        schema.pools->memory_pools->selector=0;schema.pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        schema.dependencies=std::make_shared<IgbDependenciesServices>();schema.dependencies->globals=std::make_shared<SystemStageServices>();
        schema.dependencies->globals->string_pool=pool;
        schema.dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();
            op->resume=[this,c,out=op.get()]{
                if(c.kind==IgbDependenciesServiceKind::intern_name){++interns;auto header=std::make_shared<IgbInternedString>();
                    header->value=igb_dependency_name(c.name);header->container=pool;header->references=1;out->string_result=header;
                }else if(c.kind!=IgbDependenciesServiceKind::release_string)throw std::runtime_error("Unexpected prepared string operation");
                return MessengerCallResult{true,0,{}};};return op;};
        schema.fields=std::make_shared<IgbFieldOperationsServices>();schema.fields->dependencies=schema.dependencies;
        schema.fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        schema.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();
            op->resume=[this,c,out=op.get()]{
                if(c.kind==IgbPointerListServiceKind::allocation_pool)out->pool_result=pool;
                else if(c.kind==IgbPointerListServiceKind::allocation_size)out->size_result=256;
                else throw std::runtime_error("Prepared metadata field registry unexpectedly grew");return MessengerCallResult{true,0,{}};};return op;};
        schema.fields->factory=[this](const IgbFieldOperationCall& c){auto op=std::make_shared<IgbFieldOperation>();
            if(c.source)scalar_calls.push_back(c);
            op->resume=[this,c,out=op.get()]{
                if(c.kind==IgbFieldOperationKind::allocation_pool){out->pool_result=pool;return MessengerCallResult{true,0,{}};}
                if(c.kind==IgbFieldOperationKind::allocation_size)return MessengerCallResult{true,4,{}};
                check(c.kind==IgbFieldOperationKind::field_virtual,"Unexpected prepared field operation");
                if(c.virtual_slot==0x64)return MessengerCallResult{true,4,{}};
                if(c.virtual_slot==0x5c)return MessengerCallResult{true,0,{}};
                check(c.virtual_slot==0x8c,"Unexpected prepared field virtual");
                if(c.source){++scalar_polls;if(!scalar_ready)return MessengerCallResult{false,0,"prepared scalar setter"};
                    const auto bytes=igb_buffer_bytes(c.source->buffer,c.source->offset,c.source->buffer->bytes.size()-c.source->offset);
                    auto& target=c.field->default_bytes->buffer;std::copy(bytes.begin(),bytes.end(),target->bytes.begin());
                    if(scalar_action)scalar_action(c);
                }return MessengerCallResult{true,0,{}};};return op;};
        schema.factory=[this](const IgbFieldSchemaCall& c){check(c.kind==IgbFieldSchemaServiceKind::create_field,"Unexpected prepared field destruction");
            functions.push_back(c.function);auto op=std::make_shared<IgbFieldSchemaOperation>();
            op->resume=[this,c,out=op.get()]{++field_polls;if(!fields_ready)return MessengerCallResult{false,0,"prepared specialized field factory"};
                check(c.pool==pool,"Field factory lost actual selected pool");auto f=std::make_shared<IgbDecodeRuntimeField>();
                f->vtable_word=0x81000000u+static_cast<std::uint32_t>(created.size())*0x100;f->flag_26=1;
                f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};f->global_slot_address=0;
                f->name_string=std::shared_ptr<IgbInternedString>{};f->default_mode=std::uint8_t{0};
                auto bytes=std::make_shared<IgbScratchBuffer>();bytes->bytes.assign(4,0);bytes->initialized.assign(4,1);f->default_bytes=IgbRuntimeFieldDefaultSource{bytes,0};
                created.push_back(f);out->field_result=f;out->field_address=0x100000u+static_cast<std::uint32_t>(created.size())*0x100;
                f->default_bytes_address=*out->field_address+0x40;
                return MessengerCallResult{true,0,{}};};return op;};
        services->factory=[this](const IgbMetadataDefaultsCall& c){++string_selections;auto op=std::make_shared<IgbMetadataDefaultsOperation>();
            op->resume=[this,c]{++string_polls;check(c.function==0x80071694 && c.argument==0,"String default setter call differs");
                if(!string_ready)return MessengerCallResult{false,0,"prepared original string-default setter"};
                if(string_action)string_action(c);return MessengerCallResult{true,0,{}};};return op;};
        // Two explicit inherited fields precede the original local batch.
        meta->fields->count=2;for(unsigned i=0;i<2;++i){auto inherited=std::make_shared<IgbDecodeRuntimeField>();
            inherited->name=std::make_shared<const std::string>("inherited");meta->fields->backing->at(i)=inherited;}
    }
};
void tables_and_complete_caller(){
    Fixture f;IgbMetadataDefaultsInvocation call;check(call.resume(f.services).completed,"Prepared full metadata defaults caller did not complete.");
    check(f.created.size()==25 && f.meta->fields->count==27 && f.interns==25,"Original local field count or schema traversal differs.");
    check(f.functions==std::vector<std::uint32_t>({0x8002e1ac,0x8002e1ac,0x8002e1ac,0x8002e1ac,0x800385e0,0x800385e0,0x800385e0,0x800251ec,
        0x8002e1ac,0x8002e1ac,0x8002879c,0x8002e1ac,0x8002879c,0x8002879c,0x8002879c,0x800274ec,0x800274ec,0x8002879c,
        0x8002e1ac,0x800274ec,0x800274ec,0x800274ec,0x80021e10,0x800274ec,0x800251ec}),"Original factory table identities differ.");
    for(unsigned i=0;i<25;++i){const auto& field=f.created[i];check(field==f.meta->fields->backing->at(i+2),"Actual append lost field identity.");
        check(field->ordinal==0 && field->declaring_type && field->declaring_type->lock()==f.meta,"Schema invented an ordinal or lost declaring metadata.");
        const auto global=f.services->globals->at(i);check(global && global->address==0x80561750u+i*4 && *global->slot==field,"Canonical field global address/publication differs.");
        check(field->name==*f.services->names->at(i) && field->offset==*f.services->offsets->at(i),"Schema names and offsets were swapped or displaced.");
    }
    check(*f.created[0]->name=="_id" && *f.created[10]->name=="_metaFields" && *f.created[24]->name=="_poolName","Original field name literals differ.");
    check(f.created[4]->offset==24 && f.created[5]->offset==25 && f.created[6]->offset==26 && f.created[24]->offset==96,"Byte-member offsets were widened.");
    check(f.scalar_calls.size()==7 && f.string_polls==1,"Scalar/string setter count differs.");
    for(const auto i:{0,1,8,18})check(igb_buffer_word(f.created[i]->default_bytes->buffer,0)==0xffffffffu,"Signed minus-one default lost its bits.");
    check(igb_buffer_word(f.created[2]->default_bytes->buffer,0)==1,"Major version default differs.");
    check(f.created[5]->default_bytes->buffer->bytes[0]==1,"Boolean default was not packed into one byte.");
    check(f.created[22]->default_bytes->buffer->bytes[0]==0 && f.created[22]->default_bytes->buffer->bytes[1]==4,"Alignment default was not packed big-endian16.");
    for(const auto i:{0,1,8,9,11,12,13,14,15,16,17,18,19,20,21,22,23})check(f.created[i]->flag_26==0,"Runtime-only field remained persistent.");
    for(const auto i:{2,3,4,5,6,7,10,24})check(f.created[i]->flag_26==1,"Caller cleared an unrelated persistence flag.");
    check(f.created[10]->object_type->lock()==f.field_list && f.created[10]->flag_34==1 && f.created[10]->flag_35==1 && f.created[10]->flag_3c==0,"Metadata field-list reference defaults differ.");
    check(f.created[12]->object_type->lock()==f.metadata_list && f.created[13]->object_type->lock()==f.metadata_list,"Instance/children type metadata differ.");
    check(f.created[14]->object_type->lock()==f.meta && !f.created[14]->flag_35,"Parent getter incorrectly reused another type's flags.");
    check(f.created[17]->object_type->lock()==f.object && f.created[17]->flag_35==1,"Template object did not use plain805622A4.");
    check(call.resume().completed && f.functions.size()==25,"Completed caller replayed.");
}
void pending_prefix_and_live_reads(){
    Fixture f;f.fields_ready=false;IgbMetadataDefaultsInvocation call;
    check(!call.resume(f.services).completed && f.functions==std::vector<std::uint32_t>{0x8002e1ac} && f.created.empty(),"Cold caller skipped the first real factory boundary.");
    f.fields_ready=true;f.scalar_ready=false;check(!call.resume(f.services).completed && f.created.size()==25 && f.field_polls==26,"Resuming first factory replayed batch work.");
    check(f.scalar_calls.size()==1 && f.created[0]->flag_26==1 && f.interns==0,"First scalar pause published future flags or names.");
    const auto selected=f.created[0];selected->vtable_word=0x98765432;f.scalar_ready=true;f.string_ready=false;
    check(!call.resume(f.services).completed && f.scalar_calls.size()==4 && f.string_selections==1,"Scalar continuation replayed selection or skipped string boundary.");
    check(selected->flag_26==0 && f.created[1]->flag_26==0 && f.created[8]->flag_26==1,"Pending string default lost its reached prefix.");
    f.services->metadata->metadata=metadata(); //2A164 keeps its captured receiver;29F84 reloads its own global.
    const auto replaced_type=*f.services->metadata->metadata;f.string_ready=true;
    check(call.resume(f.services).completed && f.string_selections==1 && f.string_polls==2,"Selected string setter was not retained.");
    check(f.meta->fields->count==27 && replaced_type->fields->count==0 && f.created[14]->object_type->lock()==replaced_type,"Captured receiver and live parent getter were conflated.");
}
void getter_pending_and_null(){
    Fixture f;f.services->field_list_metadata->reset();IgbMetadataDefaultsInvocation call;
    check(!call.resume(f.services).completed && f.created.size()==25 && f.created[9]->flag_26==0 && !f.created[10]->object_type,"Unknown getter global fabricated a descriptor.");
    *f.services->field_list_metadata=std::shared_ptr<IgbRuntimeMetaType>{};f.services->schema->types->metadata_pool_handle.reset();
    check(!call.resume(f.services).completed && !f.created[10]->object_type,"Null lazy getter skipped required pool selection.");
    // Replacing a cell after it was captured cannot cancel an already-entered allocation.
    *f.services->field_list_metadata=f.field_list;check(!call.resume(f.services).completed,"Entered lazy allocation was replaced by a late global cache hit.");
    Fixture plain;*plain.services->object_metadata=std::shared_ptr<IgbRuntimeMetaType>{};IgbMetadataDefaultsInvocation null_object;
    check(null_object.resume(plain.services).completed && plain.created[17]->object_type && igb_field_declaring_type_is_null(*plain.created[17]->object_type),"Plain object global null incorrectly forced allocation.");
    Fixture bad;bad.services->metadata->metadata=std::shared_ptr<IgbRuntimeMetaType>{};IgbMetadataDefaultsInvocation null_metadata;
    rejects([&]{null_metadata.resume(bad.services);},"Null receiver did not fault before batch construction.");
    check(null_metadata.faulted() && bad.functions.empty(),"Null metadata fault committed later work.");
}
void actual_lazy_getter_callers(){
    Fixture f;*f.services->field_list_metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    *f.services->metadata_list_metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    auto core=std::make_shared<IgbRuntimeTypeRegistry>();core->enabled=1;
    f.services->metadata->bootstrap=std::make_shared<IgbBootstrapServices>();
    f.services->metadata->bootstrap->runtime=std::make_shared<IgbRuntimeTypesServices>();
    f.services->metadata->bootstrap->runtime->singleton=core;
    unsigned registrations{},allocations{},polls{};bool ready{};
    std::vector<std::shared_ptr<IgbRuntimeMetaType>> outputs;std::weak_ptr<IgbRuntimeMetaType> overwritten;
    f.services->metadata->factory=[&](const IgbMetadataFactoryCall& c){auto op=std::make_shared<IgbMetadataFactoryOperation>();
        if(c.kind==IgbMetadataFactoryServiceKind::register_type){op->resume=[&]{++registrations;return MessengerCallResult{true,0,{}};};return op;}
        check(c.kind==IgbMetadataFactoryServiceKind::reflected_factory && c.pool==f.pool && c.metadata==f.meta,"Lazy getter changed metadata factory identity or pool.");
        ++allocations;op->resume=[&,out=op.get()]{++polls;if(!ready)return MessengerCallResult{false,0,"prepared reflected metadata allocation"};
            auto value=metadata();outputs.push_back(value);auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=0x500000+static_cast<std::uint32_t>(outputs.size())*0x100;
            storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(0x64,0);storage->bytes->initialized.assign(0x64,1);
            storage->bindings.push_back({0,{},value,{}});out->allocation_result={storage,0};
            if(outputs.size()==1){auto displaced=metadata();overwritten=displaced;*f.services->field_list_metadata=displaced;}
            return MessengerCallResult{true,0,{}};};return op;};
    IgbMetadataDefaultsInvocation call;check(!call.resume(f.services).completed && allocations==1 && registrations==1 && polls==1 && !f.created[10]->object_type,"Lazy field-list getter skipped its pending allocation.");
    ready=true;check(call.resume(f.services).completed && allocations==2 && registrations==2 && polls==3,"Lazy getters repeated allocation or failed to reuse live metadata-list global.");
    check(*f.services->field_list_metadata==outputs[0] && *f.services->metadata_list_metadata==outputs[1] &&
        f.created[10]->object_type->lock()==outputs[0] && f.created[12]->object_type->lock()==outputs[1] && f.created[13]->object_type->lock()==outputs[1],"Lazy getter raw publication/reload differs.");
    check(!overwritten.expired(),"Lazy getter raw overwrite performed an invented release.");
}
void live_selected_fields_and_recursion(){
    Fixture f;std::shared_ptr<IgbDecodeRuntimeField> displaced;
    f.scalar_action=[&](const IgbFieldOperationCall& c){if(c.field==f.created[0]){
        displaced=f.created[1];f.meta->fields->backing->at(3)=f.created[3];f.meta->fields->count=26;
    }};
    IgbMetadataDefaultsInvocation call;check(call.resume(f.services).completed,"Live field replacement did not complete.");
    check(displaced->flag_26==1 && f.created[3]->flag_26==0,"Default setter reused stale field backing for next access.");
    check(!f.created[24]->name && f.interns==24,"Final schema ignored its live end count.");
    Fixture recursion;IgbMetadataDefaultsInvocation recursive;recursion.scalar_action=[&](const IgbFieldOperationCall&){try{recursive.resume(recursion.services);}catch(const std::runtime_error&){};};
    rejects([&]{recursive.resume(recursion.services);},"Swallowed recursive fault did not poison the outer caller.");
    check(recursive.faulted() && recursion.created[0]->flag_26==1 && recursion.interns==0,"Recursive failure published unreached work.");
}
void adapters_and_lifetime(){
    Fixture f;auto adapter=make_native_igb_metadata_defaults_factory(f.services);IgbTypeRegistrationCall request;
    request.kind=IgbTypeRegistrationServiceKind::defaults;request.function=0x8002a164;request.metadata=metadata();
    auto operation=adapter(request);check(operation && operation->resume().completed && request.metadata->fields->count==0,"Adapter used the generic caller argument instead of8056174C.");
    request.function=0x80066b7c;check(!adapter(request),"Adapter swallowed unrelated defaults.");
    auto services=std::make_shared<IgbMetadataDefaultsServices>();std::weak_ptr<IgbMetadataDefaultsServices> weak=services;
    auto expired=make_native_igb_metadata_defaults_factory(services);services.reset();check(weak.expired(),"Defaults adapter formed an ownership cycle.");
    request.function=0x8002a164;rejects([&]{expired(request);},"Expired defaults binding was silently recreated.");
    Fixture abandoned;abandoned.scalar_ready=false;std::weak_ptr<IgbScratchBuffer> source;
    {IgbMetadataDefaultsInvocation paused;check(!paused.resume(abandoned.services).completed,"Prepared scalar setter failed to suspend.");source=abandoned.scalar_calls[0].source->buffer;}
    abandoned.scalar_calls.clear();check(!source.expired(),"Abandoned original stack argument was prematurely destroyed.");
    abandoned.services.reset();check(source.expired(),"Abandoned stack argument formed an ownership cycle.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_metadata_defaults_tests");
    tables_and_complete_caller();pending_prefix_and_live_reads();getter_pending_and_null();actual_lazy_getter_callers();live_selected_fields_and_recursion();adapters_and_lifetime();
    std::cout<<"igb_metadata_defaults_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Metadata defaults failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
