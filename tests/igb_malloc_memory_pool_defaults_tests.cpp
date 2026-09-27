#include "dw4/igb_memory_pool_registration.hpp"
#include "dw4/igb_field_values.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>

// Synthetic expectations transcribed from the sealed original 39E28 contract.
// Constructors, string interning and allocation queries are explicit lower
// endpoints. Batch insertion, scalar values, schema and finalization are real.
namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* why) { ++checks;if(!value)throw std::runtime_error(why); }
template<class F> void rejects(F&& f,const char* why) {
    bool failed{};try { f(); }catch(const std::runtime_error&) { failed=true; }check(failed,why);
}
struct Row { std::uint32_t factory;std::string_view name;std::uint32_t global,offset,table,size; };
constexpr std::array<Row,10> rows{{
    {0x8002e1ac,"_poolIndex",0x80561fb4,0x78,0x80475b60,4},
    {0x80022a90,"_elementSize",0x80561fb8,0x7c,0x80476e6c,4},
    {0x80022450,"_maxSize",0x80561fbc,0x80,0x80476f60,8},
    {0x80022450,"_size",0x80561fc0,0x88,0x80476f60,8},
    {0x80021e10,"_alignment",0x80561fc4,0x90,0x80477054,2},
    {0x8002879c,"_eventTracker",0x80561fc8,0x94,0x804762fc,4},
    {0x800385e0,"_isActive",0x80561fcc,0x70,0x80473e30,1},
    {0x8002879c,"_allocationLock",0x80561fd0,0x74,0x804762fc,4},
    {0x80031378,"_messageLevel",0x80561fd4,0x98,0x804758e4,4},
    {0x8002e1ac,"_poolOptionFlags",0x80561fd8,0x9c,0x80475b60,4}
}};
auto metadata(std::uint32_t flags=0) {
    auto result=std::make_shared<IgbRuntimeMetaType>();result->class_flags=flags;return result;
}
IgbBootstrapPointer raw(std::uint32_t address,std::size_t size) {
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(size,0xcc);
    storage->bytes->initialized.assign(size,0);return {storage,0};
}
bool unknown(const std::shared_ptr<IgbScratchBuffer>& bytes,std::size_t first=0) {
    return std::none_of(bytes->initialized.begin()+first,bytes->initialized.end(),[](auto value){return value!=0;});
}

struct Fixture {
    std::shared_ptr<IgbMemoryPoolRegistrationServices> services=std::make_shared<IgbMemoryPoolRegistrationServices>();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata(),event=metadata(),semaphore=metadata();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<IgbFieldValuesServices> values=std::make_shared<IgbFieldValuesServices>();
    IgbBootstrapPointer target_bytes=raw(0x600000,0x64);
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<IgbBootstrapPointer> physical;
    std::vector<IgbFieldOperationCall> scalars;
    std::vector<std::uint32_t> getters;
    std::function<void(unsigned,const std::shared_ptr<IgbDecodeRuntimeField>&,const IgbBootstrapPointer&)> prepare;
    std::function<void()> after_intern;
    unsigned interned{};
    IgbFieldOperationsFactory native_values;

    Fixture() {
        services->malloc_metadata_slot=std::make_shared<IgbRegistrationMetaSlot>(target);
        services->metadata_slot=std::make_shared<IgbRegistrationMetaSlot>();
        services->event_tracker_metadata=std::make_shared<IgbRegistrationMetaSlot>();
        services->malloc_message_level=0x12345678;services->malloc_pool_option_flags=0x89abcdef;
        target->fields=std::make_shared<IgbDecodeFieldRegistry>();
        target->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(32);
        target->fields->backing_bytes=raw(0x900000,128).storage->bytes;target->fields->backing_address=0x900000;
        target_bytes.storage->bindings.push_back({0,{},target});services->physical_metadata.push_back(target_bytes);
        target->word_40=0x11223344;target->factory_redirect=0x55667788;
        igb_bootstrap_write_word(target_bytes,0x40,*target->word_40);
        igb_bootstrap_write_word(target_bytes,0x3c,*target->factory_redirect);
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->pools->memory_pools->selector=0;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->pools->memory_pools->fallback=manager;
        services->schema=std::make_shared<IgbFieldSchemaServices>();services->schema->types=services->types;
        services->schema->pools=services->pools;services->schema->fields=std::make_shared<IgbFieldOperationsServices>();
        auto lists=std::make_shared<IgbPointerListServices>();services->schema->fields->pointer_lists=lists;
        lists->values={{0x600000,target,{}},{0x610000,event,{}},{0x620000,semaphore,{}}};
        lists->factory=[this](const IgbPointerListCall& call) {
            auto op=std::make_shared<IgbPointerListOperation>();op->resume=[this,call,out=op.get()] {
                if(call.kind==IgbPointerListServiceKind::allocation_pool)out->pool_result=pool;
                else { check(call.kind==IgbPointerListServiceKind::allocation_size,"Unexpected registry growth in bounded fixture.");out->size_result=128; }
                return MessengerCallResult{true,0,{}};
            };return op;
        };
        services->schema->factory=[this](const IgbFieldSchemaCall& call) {
            check(call.kind==IgbFieldSchemaServiceKind::create_field && created.size()<rows.size() &&
                call.function==rows[created.size()].factory,"Batch changed the ten original constructor calls.");
            auto op=std::make_shared<IgbFieldSchemaOperation>();op->resume=[this,out=op.get()] {
                const auto index=static_cast<unsigned>(created.size());const auto& row=rows[index];
                auto field=std::make_shared<IgbDecodeRuntimeField>();auto pointer=raw(0x100000+index*0x100,0x44);
                pointer.storage->bindings.push_back({0,{},{},{},{},field});field->physical_storage=IgbFieldStorageBinding{pointer.storage,0};
                field->name_string=std::shared_ptr<IgbInternedString>{};field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
                field->default_bytes=IgbRuntimeFieldDefaultSource{raw(0x800000+index*0x100,8).storage->bytes,0};
                field->default_bytes_address=0x800000+index*0x100;
                igb_bootstrap_write_word(pointer,0,row.table);igb_bootstrap_write_word(pointer,4,1);
                igb_bootstrap_write_halfword(pointer,0x14,static_cast<std::uint16_t>(row.size));
                igb_bootstrap_write_word(pointer,0x1c,0);igb_bootstrap_write_word(pointer,0x20,*field->default_bytes_address);
                igb_bootstrap_write_word(pointer,0x34,0);igb_bootstrap_write_word(pointer,0x38,0);
                if(prepare)prepare(index,field,pointer);
                physical.push_back(pointer);created.push_back(field);out->field_result=field;out->field_address=igb_bootstrap_address(pointer);
                return MessengerCallResult{true,0,{}};
            };return op;
        };
        auto dependencies=std::make_shared<IgbDependenciesServices>();services->schema->dependencies=dependencies;
        services->schema->fields->dependencies=dependencies;dependencies->globals=std::make_shared<SystemStageServices>();
        dependencies->globals->string_pool=pool;
        dependencies->factory=[this](const IgbDependenciesCall& call) {
            check(call.kind==IgbDependenciesServiceKind::intern_name,"Schema reached an unexpected string endpoint.");
            auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[this,call,out=op.get()] {
                ++interned;auto name=std::make_shared<IgbInternedString>();name->container=pool;
                name->value=igb_dependency_name(call.name);out->string_result=name;if(after_intern)after_intern();
                return MessengerCallResult{true,0,{}};
            };return op;
        };
        values->fields=services->schema->fields;
        values->fields->unsigned_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->unsigned_long_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->unsigned_short_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->boolean_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->enumeration_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        install_values([this](const IgbFieldOperationCall& call) {
            auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,call,out=op.get()] {
                if(call.kind==IgbFieldOperationKind::allocation_pool)out->pool_result=pool;
                else check(call.kind==IgbFieldOperationKind::allocation_size,"Unexpected default-buffer allocation in prepared-capacity fixture.");
                return MessengerCallResult{true,call.kind==IgbFieldOperationKind::allocation_size?8u:0u,{}};
            };return op;
        });
        services->types->factory=[this](const IgbTypeRegistrationCall& call) {
            check(call.kind==IgbTypeRegistrationServiceKind::get_parent &&
                (call.function==0x80030c50 || call.function==0x80026020),"Unexpected metadata getter.");
            getters.push_back(call.function);auto op=std::make_shared<IgbTypeRegistrationOperation>();
            op->metadata_result=call.function==0x80030c50?event:semaphore;
            op->resume=[] { return MessengerCallResult{true,0,{}}; };return op;
        };
    }
    void install_values(IgbFieldOperationsFactory lower) {
        native_values=make_native_igb_field_values_factory(values,std::move(lower));
        services->schema->fields->factory=[this](const IgbFieldOperationCall& call) {
            if(call.kind==IgbFieldOperationKind::field_virtual && call.virtual_slot==0x8c && call.source)scalars.push_back(call);
            return native_values(call);
        };
    }
    void pause_schema() { services->malloc_names->at(0).reset(); }
};

void exact_tables_and_unknown_inputs() {
    auto s=std::make_shared<IgbMemoryPoolRegistrationServices>();
    check(!s->event_tracker_metadata && !s->malloc_message_level && !s->malloc_pool_option_flags,
        "New canonical cells were fabricated from initialized-image labels.");
    check(s->malloc_factories!=s->factories && s->malloc_names!=s->names && s->malloc_globals!=s->globals &&
        s->malloc_offsets!=s->offsets && s->factories->size()==14 && s->malloc_factories->size()==10,
        "Concrete and abstract pool schemas share or truncate their tables.");
    for(unsigned i=0;i<rows.size();++i) {
        const auto& row=rows[i];check(s->malloc_factories->at(i)==row.factory && s->malloc_names->at(i) &&
            **s->malloc_names->at(i)==row.name && s->malloc_globals->at(i)->address==row.global &&
            s->malloc_offsets->at(i)==row.offset,"Malloc schema differs from the sealed original row.");
    }
    IgbMallocMemoryPoolDefaultsInvocation invocation;check(!invocation.resume(s).completed && !invocation.faulted(),
        "Unknown malloc receiver was mistaken for a ready/null metadata pointer.");
    s->malloc_metadata_slot=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    rejects([&]{invocation.resume(s);},"Known-null malloc receiver bypassed its actual count dereference.");
}

void actual_composition_and_five_source_widths() {
    Fixture f;auto inherited=std::make_shared<IgbDecodeRuntimeField>();f.target->fields->count=2;
    f.target->fields->backing->at(0)=inherited;
    auto adapter=make_native_igb_memory_pool_registration_type_factory(f.services);
    IgbTypeRegistrationCall call;call.kind=IgbTypeRegistrationServiceKind::defaults;call.function=0x80039e28;
    auto invocation=adapter(call);check(invocation && invocation->resume().completed,"Actual malloc defaults adapter did not complete its bounded fixture.");
    check(f.created.size()==10 && f.target->fields->count==12 && f.target->fields->backing->at(0)==inherited && f.interned==10,
        "Actual batch/schema omitted descriptors or changed inherited fields.");
    const std::array<std::vector<std::uint8_t>,5> sources{{{0xff,0xff,0xff,0xff},
        {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff},{0,4},{0x12,0x34,0x56,0x78},{0x89,0xab,0xcd,0xef}}};
    constexpr std::array<unsigned,5> indices{0,2,4,8,9};
    check(f.scalars.size()==sources.size(),"Defaults selected an extra/missing explicit scalar setter.");
    for(unsigned i=0;i<sources.size();++i) {
        const auto index=indices[i];const auto& source=*f.scalars[i].source;
        check(f.scalars[i].field==f.created[index] && f.scalars[i].vtable_word==rows[index].table &&
            source.offset==0 && source.buffer->bytes==sources[i] &&
            std::all_of(source.buffer->initialized.begin(),source.buffer->initialized.end(),[](auto v){return v==1;}),
            "Explicit setter changed selection, big-endian order or source width.");
        const auto& payload=f.created[index]->default_bytes->buffer;
        check(std::equal(sources[i].begin(),sources[i].end(),payload->bytes.begin()) &&
            std::all_of(payload->initialized.begin(),payload->initialized.begin()+sources[i].size(),[](auto v){return v==1;}) &&
            unknown(payload,sources[i].size()),"Scalar defaults wrote outside the original source width.");
    }
    for(const auto index:{1u,3u,5u,6u,7u})check(unknown(f.created[index]->default_bytes->buffer),
        "Schema finalization invented a value for an untouched default payload.");
    for(unsigned i=0;i<rows.size();++i)check(f.created[i]->size==rows[i].size && f.created[i]->offset==rows[i].offset &&
        f.created[i]->declaring_type->lock()==f.target && *f.services->malloc_globals->at(i)->slot==f.created[i] &&
        igb_bootstrap_read_word(f.physical[i],0x1c)==rows[i].global && igb_bootstrap_read_word(f.physical[i],4)==1,
        "Schema omitted original descriptor fields, global publication or creator release.");
    check(f.getters==std::vector<std::uint32_t>({0x80030c50,0x80026020}) &&
        igb_bootstrap_read_word(f.physical[5],0x38)==0x610000 && f.created[5]->object_type->lock()==f.event &&
        igb_bootstrap_read_word(f.physical[7],0x38)==0x620000 && f.created[7]->object_type->lock()==f.semaphore &&
        igb_bootstrap_read_word(f.physical[8],0x34)==0x80039b88 && f.created[8]->method_function==0x80039b88 &&
        igb_bootstrap_read_word(f.target_bytes,0x40)==0x8002bfd8 && f.target->word_40==0x8002bfd8 &&
        igb_bootstrap_read_word(f.target_bytes,0x3c)==0x55667788 && f.target->factory_redirect==0x55667788,
        "Defaults confused type, enum callback, final callback or abstract redirect stores.");
    check(invocation->resume().completed && f.interned==10 && f.scalars.size()==5,"Completed defaults replayed side effects.");
    IgbRegistrationCallbacksCall deferred;deferred.kind=IgbRegistrationCallbacksServiceKind::invoke_function;deferred.function=0x80030d50;
    check(!make_native_igb_memory_pool_registration_function_factory(f.services)(deferred),
        "Malloc metadata defaults invented full EventTracker registration.");
}

void selected_scalar_retains_callback_and_has_no_extra_read() {
    Fixture f;f.pause_schema();unsigned polls{},legacy{};bool ready{};
    std::shared_ptr<IgbFieldOperation> selected;auto native=f.services->schema->fields->factory;
    f.services->schema->fields->factory=[&](const IgbFieldOperationCall& call) {
        if(call.source && call.field==f.created[0]) {
            f.scalars.push_back(call);selected=std::make_shared<IgbFieldOperation>();
            selected->resume=[&]{++legacy;return MessengerCallResult{true,0,{}};};
            selected->resume_checked=[&](const std::function<void()>& guard) {
                ++polls;guard();return MessengerCallResult{ready,0,"Prepared no-receiver-read first scalar."};
            };return selected;
        }
        return native(call);
    };
    IgbMallocMemoryPoolDefaultsInvocation invocation;
    check(!invocation.resume(f.services).completed && f.created.size()==10 && polls==1,"First scalar did not suspend after the actual batch.");
    f.created[0]->physical_storage.reset(); // The selected virtual itself reads no receiver.
    auto replacement=std::make_shared<IgbDecodeRuntimeField>(*f.created[2]);replacement->physical_storage.reset();
    replacement->vtable_word=rows[2].table;f.target->fields->backing->at(2)=replacement;
    selected->resume_checked={};selected->resume={};f.services->schema->fields->factory=native;
    check(!invocation.resume(f.services).completed && polls==2 && !invocation.faulted(),
        "Pending selected callback was replaced or caused an unexecuted descriptor read.");
    ready=true;check(!invocation.resume(f.services).completed && polls==3 && legacy==0 && f.scalars.size()==5 &&
        f.scalars[1].field==replacement && !invocation.faulted(),"Setter completion lost checked preference, fresh next field or no-read behavior.");
}

void captured_input_and_fresh_later_global() {
    Fixture f;f.pause_schema();f.services->malloc_message_level.reset();f.services->malloc_pool_option_flags.reset();
    IgbMallocMemoryPoolDefaultsInvocation invocation;
    check(!invocation.resume(f.services).completed && f.scalars.size()==3 && f.getters.size()==2,
        "Unknown message level did not stop after the earlier defaults/type stores.");
    auto captured=f.created[8];auto replacement=std::make_shared<IgbDecodeRuntimeField>(*captured);
    replacement->physical_storage.reset();replacement->vtable_word=rows[8].table;
    f.target->fields->backing->at(8)=replacement;
    f.services->malloc_message_level=0xa1b2c3d4;
    check(!invocation.resume(f.services).completed && f.scalars.size()==4 && f.scalars.back().field==captured &&
        f.scalars.back().source->buffer->bytes==std::vector<std::uint8_t>({0xa1,0xb2,0xc3,0xd4}) &&
        captured->method_function==0x80039b88 && !replacement->method_function,
        "Unknown-global suspension recaptured field8 or skipped its later callback store.");
    auto captured9=f.created[9];auto replacement9=std::make_shared<IgbDecodeRuntimeField>(*captured9);
    replacement9->physical_storage.reset();f.target->fields->backing->at(9)=replacement9;
    f.services->malloc_pool_option_flags=0x01020304;
    check(!invocation.resume(f.services).completed && f.scalars.size()==5 && f.scalars.back().field==captured9 &&
        f.scalars.back().source->buffer->bytes==std::vector<std::uint8_t>({1,2,3,4}),
        "Option global was loaded before field9 capture or after source selection.");
}

void selected_message_source_does_not_reload_live_global() {
    Fixture f;f.pause_schema();auto ordinary=f.services->schema->fields->factory;
    bool ready{};unsigned polls{};std::shared_ptr<IgbFieldOperation> retained;
    f.services->schema->fields->factory=[&](const IgbFieldOperationCall& call) {
        auto op=ordinary(call);if(call.source && call.field==f.created[8]) {
            retained=op;auto selected=op->resume_checked;auto legacy=op->resume;
            op->resume={};op->resume_checked=[&,selected,legacy](const std::function<void()>& guard) {
                ++polls;if(!ready)return MessengerCallResult{false,0,"Prepared message-level setter."};
                return selected?selected(guard):legacy();
            };
        }return op;
    };
    IgbMallocMemoryPoolDefaultsInvocation invocation;
    check(!invocation.resume(f.services).completed && polls==1 && f.scalars.size()==4,
        "Message-level setter did not suspend after source capture.");
    f.services->malloc_message_level=0xfedcba98;f.services->malloc_pool_option_flags=0x76543210;
    retained->resume_checked={};f.services->schema->fields->factory=ordinary;ready=true;
    check(!invocation.resume(f.services).completed && polls==2 && f.scalars.size()==5 &&
        f.created[8]->default_bytes->buffer->bytes[0]==0x12 && f.created[8]->default_bytes->buffer->bytes[3]==0x78 &&
        f.scalars.back().source->buffer->bytes==std::vector<std::uint8_t>({0x76,0x54,0x32,0x10}),
        "Retained message callback reread its global or the following options setter missed a fresh load.");
}

void getter_capture_and_late_mapping() {
    Fixture f;f.pause_schema();bool ready{};unsigned selections{},polls{};
    auto ordinary=f.services->types->factory;
    f.services->types->factory=[&](const IgbTypeRegistrationCall& call) {
        if(call.function!=0x80030c50)return ordinary(call);
        ++selections;auto op=std::make_shared<IgbTypeRegistrationOperation>();
        op->resume=[&,out=op.get()]{++polls;if(!ready)return MessengerCallResult{false,0,"Prepared lazy event metadata."};
            out->metadata_result=f.event;return MessengerCallResult{true,0,{}};};return op;
    };
    IgbMallocMemoryPoolDefaultsInvocation invocation;
    check(!invocation.resume(f.services).completed && selections==1 && polls==1 && f.scalars.size()==3,
        "Object getter was not reached after the alignment setter.");
    auto captured=f.created[5];auto replacement=std::make_shared<IgbDecodeRuntimeField>(*captured);replacement->physical_storage.reset();
    f.target->fields->backing->at(5)=replacement;
    auto fresh7=std::make_shared<IgbDecodeRuntimeField>(*f.created[7]);fresh7->physical_storage.reset();f.target->fields->backing->at(7)=fresh7;
    auto& bindings=f.services->schema->fields->pointer_lists->values;
    std::erase_if(bindings,[&](const auto& binding){return binding.metadata==f.event;});
    f.services->types->factory=ordinary;ready=true;
    check(!invocation.resume(f.services).completed && polls==2 && selections==1 && !captured->object_type &&
        igb_bootstrap_read_word(f.physical[5],0x38)==0,"Missing event address published or repeated the metadata getter.");
    check(!invocation.resume(f.services).completed && polls==2,"Late address lookup reran a completed getter.");
    auto address=raw(0x610000,0x64);address.storage->bindings.push_back({0,{},f.event});address.storage->bytes.reset();
    f.services->physical_metadata.push_back(address); // Address publication does not read source metadata bytes.
    check(!invocation.resume(f.services).completed && !invocation.faulted() && polls==2 &&
        captured->object_type->lock()==f.event && !replacement->object_type &&
        igb_bootstrap_read_word(f.physical[5],0x38)==0x610000 && fresh7->object_type->lock()==f.semaphore &&
        !f.created[7]->object_type,"Late mapping lost captured field5 or the fresh field7 selection.");
}

void type_mapping_conflicts_null_and_target_faults() {
    for(const unsigned mode:{0u,1u,2u,3u}) {
        Fixture f;f.pause_schema();
        if(mode==0)f.event.reset();
        else if(mode==1)f.services->schema->fields->pointer_lists->values.push_back({0x630000,f.event,{}});
        else if(mode==2)f.services->schema->fields->pointer_lists->values.push_back({0x610000,metadata(),{}});
        else {
            auto ordinary=f.services->types->factory;
            f.services->types->factory=[&,ordinary](const IgbTypeRegistrationCall& call) {
                auto op=ordinary(call);if(call.function==0x80030c50) {
                    auto selected=op->resume;op->resume=[&,selected] {
                        auto status=selected();f.physical[5].storage->bytes=raw(0x100500,0x44).storage->bytes;return status;
                    };
                }return op;
            };
        }
        IgbMallocMemoryPoolDefaultsInvocation invocation;
        if(mode==0)check(!invocation.resume(f.services).completed && !invocation.faulted() &&
            f.created[5]->object_type && f.created[5]->object_type->expired() && igb_bootstrap_read_word(f.physical[5],0x38)==0,
            "Null metadata result required a numeric map or failed to publish known-null.");
        else {
            rejects([&]{invocation.resume(f.services);},"Conflicting type mapping or captured target replacement was accepted.");
            check(invocation.faulted() && f.scalars.size()==3 && !f.created[5]->object_type && f.interned==0,
                "Type-store fault changed canonical object type or executed later defaults.");
            rejects([&]{invocation.resume(f.services);},"Faulted type store was resumable.");
        }
    }
}

void fresh_final_metadata_and_atomic_tail_fault() {
    for(const unsigned mode:{0u,1u,2u}) {
        Fixture f;auto fresh=metadata();auto pointer=raw(0x650000,0x64);pointer.storage->bindings.push_back({0,{},fresh});
        fresh->word_40=0x01020304;igb_bootstrap_write_word(pointer,0x40,0x01020304);f.services->physical_metadata.push_back(pointer);
        f.after_intern=[&] { if(f.interned==10) {
            if(mode==1)f.services->malloc_metadata_slot->reset();
            else *f.services->malloc_metadata_slot=fresh;
            if(mode==2){pointer.storage->bytes->bytes.resize(0x43);pointer.storage->bytes->initialized.resize(0x43);}
        } };
        IgbMallocMemoryPoolDefaultsInvocation invocation;
        if(mode==2) {
            rejects([&]{invocation.resume(f.services);},"Truncated final +40 store wrote a partial word.");
            check(invocation.faulted() && fresh->word_40==0x01020304 && pointer.storage->bytes->bytes[0x40]==1 &&
                pointer.storage->bytes->bytes[0x41]==2 && pointer.storage->bytes->bytes[0x42]==3,
                "Failed final word store changed physical prefix or canonical cache.");
        }else {
            const auto result=invocation.resume(f.services);
            if(mode==1) {
                check(!result.completed && f.interned==10,"Unknown final canonical cell replayed or skipped schema.");
                *f.services->malloc_metadata_slot=fresh;check(invocation.resume(f.services).completed && f.interned==10,
                    "Late final owner repeated earlier schema effects.");
            }else check(result.completed,"Fresh final metadata did not finish.");
            check(fresh->word_40==0x8002bfd8 && igb_bootstrap_read_word(pointer,0x40)==0x8002bfd8,
                "Final callback was written to the stale initial metadata owner.");
        }
        check(f.interned==10 && f.created[9]->offset==0x9c && f.target->word_40==0x11223344 &&
            igb_bootstrap_read_word(f.target_bytes,0x40)==0x11223344,"Final-store failure lost schema prefix or modified initial owner.");
    }
}

struct LazyFixture : Fixture {
    std::shared_ptr<IgbMetadataFactoryServices> allocator=std::make_shared<IgbMetadataFactoryServices>();
    std::shared_ptr<IgbRuntimeMetaType> returned=metadata();
    IgbBootstrapPointer allocation=raw(0x660000,0x64);
    unsigned registrations{},selections{},polls{};bool ready{};
    std::function<void()> after_return;
    LazyFixture() {
        *services->event_tracker_metadata=std::shared_ptr<IgbRuntimeMetaType>{};services->metadata=allocator;
        allocation.storage->bindings.push_back({0,{},returned});allocator->metadata=metadata(4);
        allocator->bootstrap=std::make_shared<IgbBootstrapServices>();allocator->bootstrap->runtime=std::make_shared<IgbRuntimeTypesServices>();
        auto core=std::make_shared<IgbRuntimeTypeRegistry>();core->enabled=1;allocator->bootstrap->runtime->singleton=core;
        allocator->factory=[this](const IgbMetadataFactoryCall& call) {
            auto op=std::make_shared<IgbMetadataFactoryOperation>();
            if(call.kind==IgbMetadataFactoryServiceKind::register_type) {
                ++registrations;op->resume=[] {return MessengerCallResult{true,0,{}};};
            }else {
                check(call.kind==IgbMetadataFactoryServiceKind::reflected_factory && call.pool==pool,
                    "EventTracker lazy allocation changed the actual29E64 lower call.");++selections;
                op->resume=[this,out=op.get()] {++polls;if(!ready)return MessengerCallResult{false,0,"Prepared metadata allocation."};
                    out->allocation_result=allocation;if(after_return)after_return();return MessengerCallResult{true,0,{}};};
            }return op;
        };
    }
};

void lazy_event_readiness_and_retained_allocation() {
    auto services=std::make_shared<IgbMemoryPoolRegistrationServices>();IgbEventTrackerMetadataLookupInvocation waiting;
    check(!waiting.resume(services).completed,"Unknown EventTracker cell fabricated readiness.");
    services->event_tracker_metadata=std::make_shared<IgbRegistrationMetaSlot>();
    check(!waiting.resume(services).completed,"Unknown EventTracker pointer was treated as null.");
    auto unregistered=metadata(0);*services->event_tracker_metadata=unregistered;
    check(waiting.resume(services).completed && waiting.result()==unregistered && unregistered->class_flags==0,
        "Allocation-only EventTracker getter incorrectly required registration flag4.");
    auto adapter=make_native_igb_memory_pool_registration_type_factory(services);
    IgbTypeRegistrationCall call;call.kind=IgbTypeRegistrationServiceKind::get_parent;call.function=0x80030c50;
    auto getter=adapter(call);check(getter && getter->resume().completed && getter->metadata_result==unregistered,
        "Exact30C50 type adapter missed the allocation-only getter.");
    for(const bool null_result:{false,true}) {
        LazyFixture f;auto captured=f.services->event_tracker_metadata;IgbEventTrackerMetadataLookupInvocation invocation;
        check(!invocation.resume(f.services).completed && f.registrations==1 && f.selections==1 && f.polls==1 && !captured->value(),
            "Lazy getter published incomplete metadata or ran the full EventTracker registration.");
        auto different=metadata(4);f.services->event_tracker_metadata=std::make_shared<IgbRegistrationMetaSlot>(different);
        f.services->metadata=std::make_shared<IgbMetadataFactoryServices>();f.allocator->factory={};f.ready=true;
        if(null_result)f.allocation={};
        check(invocation.resume(f.services).completed && invocation.result()==(null_result?nullptr:f.returned) &&
            captured->value()==invocation.result() && f.services->event_tracker_metadata->value()==different &&
            f.selections==1 && f.polls==2 && f.returned->class_flags==0,
            "Pending lazy lookup recaptured allocator/cell or fabricated class readiness.");
    }
    LazyFixture f;f.ready=true;auto fresh=metadata();bool changed{};auto slot=f.services->event_tracker_metadata;
    IgbEventTrackerMetadataLookupInvocation invocation;
    check(invocation.resume(f.services,[&] {if(!changed && *slot && **slot==f.returned){*slot=fresh;changed=true;}}).completed &&
        changed && invocation.result()==fresh && f.selections==1,"Lazy getter failed its fresh post-publication canonical reload.");
}

void lazy_unconsumed_results_survive_caught_guard_faults() {
    for(const bool factory_result:{false,true}) {
        auto f=std::make_unique<LazyFixture>();f->ready=true;auto owner=f->services;
        std::weak_ptr<IgbMemoryPoolRegistrationServices> weak_owner=owner;
        std::weak_ptr<ModelSceneObject> weak_pool=f->pool;
        std::weak_ptr<IgbObjectStorage> weak_storage=f->allocation.storage;
        std::weak_ptr<IgbRuntimeMetaType> weak_metadata=f->returned;
        bool child_returned{};if(factory_result)f->after_return=[&]{child_returned=true;};
        const auto pool_count=f->pool.use_count();
        {
            IgbEventTrackerMetadataLookupInvocation invocation;
            rejects([&]{invocation.resume(owner,[&] {
                if((factory_result && child_returned) || (!factory_result && f->pool.use_count()>pool_count))
                    throw std::runtime_error("Prepared enclosing fault after a completed child.");
            });},"Borrowed fault was ignored after a completed lazy child.");
            check(invocation.faulted() && !owner->event_tracker_metadata->value(),"Faulted getter published its unconsumed result.");
        }
        owner->schema.reset();owner->types.reset();owner->pools.reset();owner->metadata.reset();f.reset();
        check(!weak_pool.expired() && (!factory_result || (!weak_storage.expired() && !weak_metadata.expired())),
            "Getter destruction discarded an unconsumed completed child result.");
        owner.reset();check(weak_owner.expired() && weak_pool.expired() && (!factory_result || weak_storage.expired()),
            "Unfinished getter retained a callback/service ownership cycle.");
    }
}

void enum_store_fault_preserves_value_and_retains_old_owner() {
    auto f=std::make_unique<Fixture>();f->pause_schema();auto owner=f->services;auto ordinary=f->services->schema->fields->factory;
    std::weak_ptr<IgbScratchBuffer> old_bytes;bool ready{};unsigned polls{};
    f->services->schema->fields->factory=[&](const IgbFieldOperationCall& call) {
        auto op=ordinary(call);if(call.source && call.field==f->created[8]) {
            auto checked=op->resume_checked;auto legacy=op->resume;
            op->resume={};op->resume_checked=[&,checked,legacy](const std::function<void()>& guard) {
                ++polls;if(!ready)return MessengerCallResult{false,0,"Prepared enum completion."};
                auto result=checked?checked(guard):legacy();old_bytes=f->physical[8].storage->bytes;
                f->physical[8].storage->bytes=raw(0x100800,0x44).storage->bytes;return result;
            };
        }return op;
    };
    {
        IgbMallocMemoryPoolDefaultsInvocation invocation;
        check(!invocation.resume(owner).completed && polls==1,"Enum setter did not retain its pending callback.");
        ready=true;rejects([&]{invocation.resume(owner);},"Enum +34 accepted replaced captured bytes after completed value copy.");
        check(invocation.faulted() && f->created[8]->default_bytes->buffer->bytes[0]==0x12 &&
            f->created[8]->default_bytes->buffer->initialized[0]==1 && !f->created[8]->method_function && f->interned==0,
            "Enum identity fault lost copied value prefix or published the callback.");
    }
    ordinary={};owner->schema.reset();f.reset();check(!old_bytes.expired(),"Failed defaults dropped its reached old descriptor byte owner.");
    owner.reset();check(old_bytes.expired(),"Retained field access formed a service ownership cycle.");
}

void pending_defaults_keep_captured_bytes_without_cycles() {
    auto f=std::make_unique<Fixture>();auto owner=f->services;
    std::weak_ptr<IgbMemoryPoolRegistrationServices> weak_owner=owner;
    auto& mappings=owner->schema->fields->pointer_lists->values;
    std::erase_if(mappings,[&](const auto& entry){return entry.metadata==f->event;});
    std::weak_ptr<IgbScratchBuffer> old_bytes;
    {
        IgbMallocMemoryPoolDefaultsInvocation invocation;
        check(!invocation.resume(owner).completed && f->getters==std::vector<std::uint32_t>({0x80030c50}) &&
            f->scalars.size()==3,"Missing dependency identity did not retain the reached type-store boundary.");
        old_bytes=f->physical[5].storage->bytes;
        f->physical[5].storage->bytes=raw(0x100500,0x44).storage->bytes;
        // Abandon while pending, without performing another target access.
    }
    owner->schema.reset();owner->types.reset();f.reset();
    check(!old_bytes.expired(),"Pending defaults abandoned their already captured descriptor byte owner.");
    owner.reset();check(weak_owner.expired() && old_bytes.expired(),"Pending defaults retained a callback/service cycle.");
}

void unconsumed_type_result_survives_caught_parent_fault() {
    auto f=std::make_unique<Fixture>();auto owner=f->services;
    std::weak_ptr<IgbMemoryPoolRegistrationServices> weak_owner=owner;
    std::weak_ptr<IgbRuntimeMetaType> weak_result;
    std::weak_ptr<IgbTypeRegistrationOperation> weak_operation;
    {
        IgbMallocMemoryPoolDefaultsInvocation invocation;
        owner->types->factory=[&,weak_owner](const IgbTypeRegistrationCall& call) {
            check(call.function==0x80030c50,"Result-retention fixture passed its first metadata getter.");
            auto op=std::make_shared<IgbTypeRegistrationOperation>();weak_operation=op;
            op->resume=[&,keep=weak_owner.lock(),out=op.get()] {
                rejects([&]{invocation.resume(keep);},"Caught metadata callback recursion did not invalidate outer defaults.");
                out->metadata_result=metadata();weak_result=out->metadata_result;
                return MessengerCallResult{true,0,{}};
            };return op;
        };
        rejects([&]{invocation.resume(owner);},"Outer defaults consumed a type result after its caught recursive fault.");
        check(invocation.faulted() && igb_bootstrap_read_word(f->physical[5],0x38)==0 && !f->created[5]->object_type &&
            f->scalars.size()==3 && f->interned==0,"Faulted getter published type metadata or later default effects.");
    }
    check(weak_operation.expired() && !weak_result.expired(),"Unfinished defaults kept the operation closure instead of its returned owner.");
    owner->schema.reset();owner->types.reset();f.reset();owner.reset();
    check(weak_owner.expired() && weak_result.expired(),"Unconsumed result retention created an ownership cycle.");
}

void constructor_and_schema_recursion_keep_exact_prefixes() {
    for(const bool during_schema:{false,true}) {
        Fixture f;IgbMallocMemoryPoolDefaultsInvocation invocation;
        if(during_schema)f.after_intern=[&] {
            rejects([&]{invocation.resume(f.services);},"Recursive defaults during actual schema did not fault.");
        };
        else {
            auto original=f.services->schema->factory;
            f.services->schema->factory=[&,original](const IgbFieldSchemaCall& call) {
                auto op=original(call);auto selected=op->resume;
                op->resume=[&,selected] {
                    const auto result=selected();
                    rejects([&]{invocation.resume(f.services);},"Recursive defaults during constructor did not fault.");return result;
                };return op;
            };
        }
        rejects([&]{invocation.resume(f.services);},"Caught constructor/schema recursion allowed outer defaults to continue.");
        check(invocation.faulted() && f.target->word_40==0x11223344 &&
            igb_bootstrap_read_word(f.physical[0],0x1c)==0,"Reentrant defaults published schema or final metadata callback.");
        if(during_schema)check(f.created.size()==10 && f.target->fields->count==10 && f.scalars.size()==5 && f.interned==1,
            "Schema recursion lost the complete scalar prefix or replayed interning.");
        else check(f.created.size()==1 && f.target->fields->count==0 && igb_bootstrap_read_word(f.physical[0],4)==1 &&
            f.scalars.empty() && f.interned==0,"Constructor recursion retained/appended/released after the caught fault.");
        rejects([&]{invocation.resume(f.services);},"Constructor/schema parent fault was not permanent.");
    }
}

void caught_outer_recursion_stops_actual_buffer_publication() {
    Fixture f;f.prepare=[](unsigned index,const auto& field,const IgbBootstrapPointer& pointer) {
        if(index==0){field->default_bytes=IgbRuntimeFieldDefaultSource{};field->default_bytes_address=0;igb_bootstrap_write_word(pointer,0x20,0);}
    };
    auto payload=raw(0x820000,4).storage->bytes;unsigned allocations{};IgbMallocMemoryPoolDefaultsInvocation invocation;
    f.install_values([&](const IgbFieldOperationCall& call) {
        auto op=std::make_shared<IgbFieldOperation>();op->resume=[&,call,out=op.get()] {
            if(call.kind==IgbFieldOperationKind::object_pool || call.kind==IgbFieldOperationKind::allocation_pool)out->pool_result=f.pool;
            else if(call.kind==IgbFieldOperationKind::allocation_size)return MessengerCallResult{true,8,{}};
            else {
                check(call.kind==IgbFieldOperationKind::allocate_default && call.bytes==4,"Reentry fixture reached a different buffer allocation.");
                ++allocations;rejects([&]{invocation.resume(f.services);},"Recursive outer defaults did not fault.");
                out->buffer_result={payload,0};out->buffer_address=0x820000;
            }return MessengerCallResult{true,0,{}};
        };return op;
    });
    rejects([&]{invocation.resume(f.services);},"Caught recursion allowed actual buffer operation to publish.");
    check(invocation.faulted() && allocations==1 && f.created.size()==10 && f.target->fields->count==10 &&
        !f.created[0]->default_bytes->buffer && igb_bootstrap_read_word(f.physical[0],0x20)==0 && unknown(payload) &&
        f.getters.empty() && f.interned==0 && f.target->word_40==0x11223344,
        "Nested buffer operation continued after parent fault instead of preserving its exact prefix.");
    rejects([&]{invocation.resume(f.services);},"Faulted defaults resumed after caught recursion.");
}
} // namespace

int main() {
    try {
        exact_tables_and_unknown_inputs();actual_composition_and_five_source_widths();
        selected_scalar_retains_callback_and_has_no_extra_read();captured_input_and_fresh_later_global();
        selected_message_source_does_not_reload_live_global();
        getter_capture_and_late_mapping();type_mapping_conflicts_null_and_target_faults();
        fresh_final_metadata_and_atomic_tail_fault();lazy_event_readiness_and_retained_allocation();
        lazy_unconsumed_results_survive_caught_guard_faults();enum_store_fault_preserves_value_and_retains_old_owner();
        pending_defaults_keep_captured_bytes_without_cycles();unconsumed_type_result_survives_caught_parent_fault();
        constructor_and_schema_recursion_keep_exact_prefixes();
        caught_outer_recursion_stops_actual_buffer_publication();
        std::cout<<"Malloc defaults checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error) {
        std::cerr<<"Malloc defaults failure after "<<checks<<" checks: "<<error.what()<<'\n';return 1;
    }
}
